#include "config.h"
#include "c_api.h"
#include "dataset.h"
#include "define.h"
#include "utils.h"
#include "bin.h"
#include "boosting.h"
#include "objective_function.h"
#include "metric.h"
#include "regression_metric.hpp"
#include "predictor.hpp"


#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <mutex>
#include <functional>


namespace Tiny_LightGBM {

class Booster {
public:

	//����booster�ļ����������ڴ������.md������ϸ����
	Booster(const Dataset* train_data) {

		train_data_ = train_data;

		boosting_.reset(Boosting::CreateBoosting());

		CreateObjectiveAndMetrics();

		//��ͷϷ��GBDT
		boosting_->Init(train_data_ ,objective_fun_.get(),Utils::ConstPtrInVectorWrapper<Metric>(train_metric_) );


	}


	void CreateObjectiveAndMetrics() {

		//Ŀ�꺯�������Ż������������L2loss�ع�
		//Ŀ�꺯����Ӱ�칫ʽ���㣬��һ���׵��ȵ�
		objective_fun_.reset(ObjectiveFunction::CreateObjectiveFunction());
		objective_fun_->Init(train_data_->metadata(),train_data_->num_data());

		//����ָ�꣬���Ǻ���Ҫ����ʱ����
		train_metric_.clear();
		auto metric = std::unique_ptr<Metric>(Metric::CreateMetric(1));
		metric->Init(train_data_->metadata(), train_data_->num_data());
		train_metric_.push_back(std::move(metric));
		train_metric_.shrink_to_fit();

	}

	int LGBM_BoosterUpdateOneIter(void* handle, int* is_finished) {

		Booster* ref_booster = reinterpret_cast<Booster*>(handle);
		if (ref_booster->TrainOneIter()) {
			*is_finished = 1;
		}
		else {
			*is_finished = 0;
		}

	}

	bool TrainOneIter() {

		return boosting_->TrainOneIter(nullptr, nullptr);
	}

	void Predict( int nrow,
				std::function<std::vector<std::pair<int, double>>(int row_idx)> get_row_fun,
				double* out_result, int* out_len) {

		/*
		����Ԥ��Ϳ����ˣ����¶�����
		bool is_predict_leaf = false;
		bool is_raw_score = false;
		bool predict_contrib = false;
		*/

		Predictor predictor(boosting_.get());
		//�ع�
		int num_pred_in_one_row = 1;

		auto pred_fun = predictor.GetPredictFunction();

		for (int i = 0; i < nrow; ++i) {
			
			auto one_row = get_row_fun(i);
			auto pred_wrt_ptr = out_result + static_cast<size_t>(num_pred_in_one_row) * i;
			pred_fun(one_row, pred_wrt_ptr);
			
		}
		*out_len = num_pred_in_one_row * nrow;
	}


private:
	const Dataset* train_data_;
	std::unique_ptr<Boosting> boosting_;

	std::unique_ptr<ObjectiveFunction> objective_fun_;

	std::vector<std::unique_ptr<Metric>> train_metric_;
};

}










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

//ʵ���Ϲ���dataset
Dataset* ConstructFromSampleData(double** sample_values,
								int** sample_indices,
								int num_col,
								const int* num_per_col,
								int num_row) {
	//����ο�BinMapper��Ľ���
	std::vector<std::unique_ptr<BinMapper>> bin_mappers(num_col);

	//���ÿ��ԭʼ��feature������bin��������ԭʼ�����ݷֲ�������װͰ��
	//�ò���ʵ����xgboost֮ǰ���С����Դ������ѵ�����̡�
	for (int i = 0; i < num_col; ++i) {

		bin_mappers[i].reset(new BinMapper());
		bin_mappers[i]->FindBin(sample_values[i] , num_per_col[i] , num_row);

	}

	//����ÿ��feature��һ��BinMapper֮�󣬾Ϳ����������dataset������
	auto dataset = std::unique_ptr<Dataset>(new Dataset(num_row));
	dataset->Construct(bin_mappers, sample_indices, num_per_col, num_row);


}

//��һ��������ڣ�����dataset
//outʵ���Ϸ���datasetָ�룬 ������ ctypes.byref(dataset) dataset = ctypes.c_void_p()
//���out��void** ����
int LGBM_DatasetCreateFromMat(const void* data,
								const void* label,
								int num_row,
								int num_col,
								void** out) {
	//ʹ��ȫ��Ĭ�ϲ���
	//tiny-lightgbm��ʵ��ʵ���Ϻܴ�̶�ʡ���˲���
	//���������еĲ�������config�У������ָ��
	Config config;

	//ͨ�������������ȡ���ݼ���һ�����ݡ���һ��sample
	std::function<std::vector<double>(int row_idx)> get_row_fun = RowFunctionFromDenseMatric(data ,num_row,num_col );

	std::vector<std::vector<double>> sample_values(num_col);
	std::vector<std::vector<int>> sample_idx(num_col);

	for (int i = 0; i < num_row; ++i) {

		auto row = get_row_fun(static_cast<int>(i));

		//ע����������Ԫ���ų��� 0 ������˵�ӽ�0�����ݡ�
		//const double kZeroThreshold = 1e-35f;
		for (int k = 0; k < row.size(); ++k) {
			if (std::fabs(row[k]) > kZeroThreshold || std::isnan(row[k])) {

				sample_values[k].emplace_back(row[k]);
				sample_idx[k].emplace_back(static_cast<int>(i));
			}
		}
	}

	//����dataset�࣬����dataset�ܹ��ο��������.md
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
		//�������dataset�࣬��Ҫһ��һ�е�������ݽ�ȥ��
		ret->PushOneRow(start_row + i, onw_row);
	}


	//���ϲ���ֻ�����data����û�����label
	bool is_success = false;
	is_success = ret->SetFloatField(reinterpret_cast<const float*>(label));

	*out = ret.release();

	//��������
	return 0;
}

int LGBM_BoosterCreate(const void* train_data ,
					    void** out) {
	//ת��һ��dataset��
	const Dataset* p_train_data = reinterpret_cast<const Dataset*>(train_data);

	//���������࣬booster
	auto ret = std::unique_ptr<Booster>(new Booster(p_train_data));
	*out = ret.release();
}

int LGBM_BoosterPredictForMat(void* model,
									const void* data,
									int nrow,
									int ncol,
						
									int* out_len,
									double* out_result) {
	Booster* ref_booster = reinterpret_cast<Booster*>(model);
	auto get_row_fun = RowPairFunctionFromDenseMatric(data, nrow, ncol);

	ref_booster->Predict( nrow, get_row_fun,out_result, out_len);

}

std::function<std::vector<std::pair<int, double>>(int row_idx)>
RowPairFunctionFromDenseMatric(const void* data, int num_row, int num_col) {
	auto inner_function = RowFunctionFromDenseMatric(data, num_row, num_col);
	if (inner_function != nullptr) {
		return [inner_function](int row_idx) {
			auto raw_values = inner_function(row_idx);
			std::vector<std::pair<int, double>> ret;
			for (int i = 0; i < static_cast<int>(raw_values.size()); ++i) {
				if (std::fabs(raw_values[i]) > kZeroThreshold || std::isnan(raw_values[i])) {
					ret.emplace_back(i, raw_values[i]);
				}
			}
			return ret;
		};
	}
	return nullptr;
}

