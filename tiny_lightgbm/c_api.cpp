#include "config.h"
#include "c_api.h"
#include "dataset.h"
#include "define.h"
#include "utils.h"
#include "bin.h"


#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <mutex>
#include <functional>


using namespace Tiny_LightGBM;


std::function<std::vector<double>(int row_idx)> RowFunctionFromDenseMatric(const void* data ,
															int num_row,
															int num_col) {


	//reinterpret_cast ��cpython�õıȽ϶࣬ʵ�����Ǵ������ṹ����ʼ�ӳ�Ա��
	//����ο� https://www.zhihu.com/question/302752247
	const float* data_ptr = reinterpret_cast<const float*>(data);


	// ����lambda ����
	return [=](int row_idx) {

		std::vector<double> ret(num_col);
		auto tmp_ptr = data_ptr + static_cast<int>(num_col)*row_idx;

		for (int i = 0; i < num_col; ++i) {
			ret[i] = static_cast<double>(*(tmp_ptr + i));
		}
		return ret;
	};
}


Dataset* ConstructFromSampleData(double** sample_values,
								int** sample_indices,
								int num_col,
								const int* num_per_col,
								int num_row) {

	std::vector<std::unique_ptr<BinMapper>> bin_mappers(num_col);
	for (int i = 0; i < num_col; ++i) {

		bin_mappers[i].reset(new BinMapper());
		bin_mappers[i]->FindBin(sample_values[i] , num_per_col[i] , num_row);

	}
	auto dataset = std::unique_ptr<Dataset>(new Dataset(num_row));

	dataset->Construct(bin_mappers, sample_indices, num_per_col, num_row);


}


int LGBM_DatasetCreateFromMat(const void* data,
								const void* label,
								int num_row,
								int num_col,
								void** out) {
	//ʹ��ȫ��Ĭ�ϲ���
	Config config;


	//ͨ�������������ȡ���ݼ���һ�����ݡ���һ��sample
	std::function<std::vector<double>(int row_idx)> get_row_fun = RowFunctionFromDenseMatric(data ,num_row,num_col );


	std::vector<std::vector<double>> sample_values(num_col);
	std::vector<std::vector<int>> sample_idx(num_col);


	for (int i = 0; i < num_row; ++i) {

		auto row = get_row_fun(static_cast<int>(i));


		//ע����������Ԫ���ų��� 0 
		for (int k = 0; k < row.size(); ++k) {
			if (std::fabs(row[k]) > kZeroThreshold || std::isnan(row[k])) {

				sample_values[k].emplace_back(row[k]);
				sample_idx[k].emplace_back(static_cast<int>(i));
			}
		}
	}

	std::unique_ptr<Dataset> ret;
	ret.reset(
		ConstructFromSampleData(
			Utils::Vector2Ptr<double>(sample_values).data(),
			Utils::Vector2Ptr<int>(sample_idx).data(),
			static_cast<int>(num_col),
			Utils::VectorSize<double>(sample_values).data(),
			static_cast<int>(num_row)

		)
	);


	int start_row = 0;
	for (int i = 0; i < num_row; ++i) {
		auto onw_row = get_row_fun(i);

		ret->PushOneRow(start_row + i, onw_row);
	}


	*out = ret.release();

	//��������
	return 0;
}