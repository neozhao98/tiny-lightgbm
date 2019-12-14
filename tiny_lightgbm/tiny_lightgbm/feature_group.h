#pragma once

#include "bin.h"



#include <cstdio>
#include <memory>
#include <vector>



namespace Tiny_LightGBM {

class Dataset;
class FeatureGroup {

public:

	friend Dataset;


	//û��sparse�Ŀ���
	FeatureGroup(int num_feature , 
				std::vector<std::unique_ptr<BinMapper>>& bin_mappers , 
				int num_data 
				):num_feature_(num_feature) {
		
		//����featuregroupֻ��һ��feature����feature������10��bin��
		//������Ҫ������һ��bin��0λ�á�
		num_total_bin_ = 1;
		bin_offsets_.emplace_back(num_total_bin_);

		
		for (int i = 0; i < num_feature; ++i) {
			bin_mappers_.emplace_back(bin_mappers[i].release());
			auto num_bin = bin_mappers_[i]->num_bin();
			
			//ɧ����
			//������Կ�������ͬ��feature���д���ʱ�ǲ�һ���ġ�
			//���ԭʼfeature��BinMapper��һ��bin����value 0���ڵ�
			//��ô��num_bin -= 1
			if (bin_mappers_[i]->GetDefaultBin() == 0) {
				num_bin -= 1;
			}
			//�����ֹһ��feature�Ļ������Կ�����ͬfeature��bin�ĸ����õ��˵���
			//����feature1��bin��30����feature2��bin��40����
			//��ôfeature1��ռ��1-30��λ�ã�feature2��ռ��30-70���������feature��default bin������0��
			//��0���λ�þͿ����˳������������ἰ��ôʹ��
			num_total_bin_ += num_bin;
			bin_offsets_.emplace_back(num_total_bin_);

		}

		//Ĭ�����е�feature�����ܼ��ģ�DenseBin
		bin_data_.reset(Bin::CreateBin(num_data, num_total_bin_));


	}


	inline void PushData(int sub_feature_idx, int line_idx, double value) {

		//�õ���ǰ���featureӦ����䵽�ĸ�bin����ȥ
		int bin = bin_mappers_[sub_feature_idx]->ValueToBin(value);

		//���value��0����Ӧ����䵽default bin��λ�ã�������
		//��Ϊ�������е����ݳ�ʼ�����Ƿ���bin 0��λ�ã����������DenseBin�Ĺ�������
		//��Ҫע����ǣ������EFB������£������һ���͵ڶ���feature��������value0
		//��ô��Ӧ�÷���bin0��û��������
		if (bin == bin_mappers_[sub_feature_idx]->GetDefaultBin()) { return; }

		//bin_offsets��ֵ��[1,31,71]��ע���ʼ��1������0
		bin += bin_offsets_[sub_feature_idx];

		//���赱ǰfeature groupֻ��һ��feature������default bin =0
		//�������һ��value=0����ô��֮ǰ�ͻ�return
		//�������һ��value>0����ô������bin-=1
		//��������Ļ���bin 1�ͻ�û���κ����������棨��Ϊbin += bin_offsets֮�����ٶ���2��

		//�������赱ǰfeature group�ж��feature������ĳһ��feature��default binҲ��0
		//��ôҲ��һ���Ŀ���

		//����Ĳ�����Ȼ���ӣ�����˼���ǣ�
		//���feature��Ϊһ��group������£�ÿ��feature��value 0 ��Ӧ�÷���һ��bin 0λ�ã�
		//��������Ϊ��ͬ��bin
		if (bin_mappers_[sub_feature_idx]->GetDefaultBin() == 0) {
			bin -= 1;

		}
		//������
		bin_data_->Push(line_idx, bin);


	}

	inline int Split(
		int sub_feature,
		const uint32_t* threshold,
		int num_threshold,
		bool default_left,
		int* data_indices, int num_data,
		int* lte_indices, int* gt_indices) const {

		uint32_t min_bin = bin_offsets_[sub_feature];
		uint32_t max_bin = bin_offsets_[sub_feature + 1] - 1;
		uint32_t default_bin = bin_mappers_[sub_feature]->GetDefaultBin();
		
			
		return bin_data_->Split(min_bin, max_bin, default_bin, default_left,
			*threshold, data_indices, num_data, lte_indices, gt_indices);
		
	}


private:
	std::vector<int> bin_offsets_;
	int num_total_bin_;
	int num_feature_;
	std::vector<std::unique_ptr<BinMapper>> bin_mappers_;

	std::unique_ptr<Bin> bin_data_;


};

}
