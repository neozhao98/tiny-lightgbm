#include "dataset.h"
#include "bin.h"
#include "feature_group.h"


namespace Tiny_LightGBM {


std::vector<std::vector<int>> NoGroup(const std::vector<int>& used_features) {
	std::vector<std::vector<int>> features_in_group;
	features_in_group.resize(used_features.size());

	for (size_t i = 0; i < used_features.size(); ++i) {
		features_in_group[i].emplace_back(used_features[i]);
	}
	return features_in_group;
}

Dataset::Dataset() {}
Dataset::Dataset() {}

Dataset::Dataset(int num_data) {
	num_data_ = num_data;

	//??????
	group_bin_boundaries_.push_back(0);
}

//����������dataset
void Dataset::Construct(std::vector<std::unique_ptr<BinMapper>>& bin_mappers,
						int ** sample_non_zeros_indices,
						const int* num_per_col,
						int num_row) {

	//�򵥵ĳ�ʼ��
	num_total_features_ = static_cast<int>(bin_mappers.size());
	std::vector<int> used_features;
	for (int i = 0; i < static_cast<int>(bin_mappers.size()); ++i) {
		used_features.emplace_back(i);
	}

	//����ʵ���Ϻܶ๤����Ϊ��EFBչ���ģ����к���

	//��vector<int> ת���� vector<vector<int>>
	//�����������EFB��group����
	auto feature_in_group = NoGroup(used_features);


	//����ʡ�Ե�EFB����feature group����ֻ��һ��feature��û�кϲ�����
	//EFB��ʵ����Ҫ����������feature�ϲ���������˼���ѵ��ʱ��
	//

	//����ѡ�е�feature��ȫ��������group*cnt_in_group
	//�����ܣ�����ȫ��feature���ῼ�Ǽ���
	num_features_ = 0;
	for (const auto& fs : feature_in_group) {
		num_features_ += static_cast<int>(fs.size());
	}

	//����ʵ������Ϊ�˼�¼featuregroup֮���feature��ԭʼfeature�Ĺ�ϵ
	//����������
	int cur_fidx = 0;
	used_feature_map_ = std::vector<int>(num_total_features_, -1);
	real_feature_idx_.resize(num_features_);
	feature2group_.resize(num_features_);
	feature2subfeature_.resize(num_features_);
	num_groups_ = static_cast<int>(feature_in_group.size());

	//���ÿ��feature groupȥ����
	for (int i = 0; i < num_groups_; ++i) {
		auto cur_features = feature_in_group[i];
		int cur_cnt_features = static_cast<int>(cur_features.size());

		std::vector<std::unique_ptr<BinMapper>> cur_bin_mappers;

		for (int j = 0; j < cur_cnt_features; ++j) {
			int real_fidx = cur_features[j];
			used_feature_map_[real_fidx] = cur_fidx;
			real_feature_idx_[cur_fidx] = real_fidx;
			feature2group_[cur_fidx] = i;
			feature2subfeature_[cur_fidx] = j;
			cur_bin_mappers.emplace_back(bin_mappers[real_fidx].release());
			++cur_fidx;
		}

		//ͨ��featuregroup�µĶ������һ��feature��BinMapperȥ����featuregroup
		feature_groups_.emplace_back(
			std::unique_ptr<FeatureGroup>(new FeatureGroup(cur_cnt_features, cur_bin_mappers, num_row)));
	}

	feature_groups_.shrink_to_fit();
	group_bin_boundaries_.clear();
	int num_total_bin = 0;

	for (int i = 0; i < num_groups_; ++i) {
		num_total_bin += feature_groups_[i]->num_total_bin_;
		//����group_bin_boundaries_������������feature group��һ���߽�
		//����֮ǰ�ᵽ������feature��һ��bin30��һ��bin40����ô����70+1
		group_bin_boundaries_.push_back(num_total_bin);
	}
	int last_group = 0;
	group_feature_start_.reserve(num_groups_);
	group_feature_cnt_.reserve(num_groups_);
	//ͳ��һ��feature group������feature���
	//��Ȼ��tiny-lightgbmû��EFB
	group_feature_start_.push_back(0);
	group_feature_cnt_.push_back(1);

	for (int i = 1; i < num_features_; ++i) {
		const int group = feature2group_[i];
		if (group == last_group) {
			group_feature_cnt_.back() = group_feature_cnt_.back() + 1;
		}
		else {
			group_feature_start_.push_back(i);
			group_feature_cnt_.push_back(1);
			last_group = group;
		}
	}

}



bool Dataset::SetFloatField(const float* label) {

	metadata_.SetLabel(label, num_data_);

	return true;

}

void Dataset::ConstructHistograms(const std::vector<int>& is_feature_used,
	const int* data_indices, int num_data,
	int leaf_idx,
	std::vector<std::unique_ptr<OrderedBin>>& ordered_bins,
	const float* gradients, const float* hessians,
	float* ordered_gradients, float* ordered_hessians,
	bool is_constant_hessian,
	HistogramBinEntry* hist_data) const {

	if (leaf_idx < 0 || num_data < 0 || hist_data == nullptr) { return; }

	std::vector<int> used_group;
	used_group.reserve(num_groups_);

	for (int group = 0; group < num_groups_; ++group) {
		const int f_cnt = group_feature_cnt_[group];
		bool is_group_used = false;

		for (int j = 0; j < f_cnt; ++j) {
			const int fidx = group_feature_start_[group] + j;
			if (is_feature_used[fidx]) {
				is_group_used = true;
				break;
			}
		}
		if (is_group_used) {
			used_group.push_back(group);
		}
	}
	int num_used_group = static_cast<int>(used_group.size());
	auto ptr_ordered_grad = gradients;
	auto ptr_ordered_hess = hessians;

	for (int i = 0; i < num_data; ++i) {
		ordered_gradients[i] = gradients[data_indices[i]];
	}
	ptr_ordered_grad = ordered_gradients;
	ptr_ordered_hess = ordered_hessians;

	for (int gi = 0; gi < num_used_group; ++gi) {
		int group = used_group[gi];
		auto data_ptr = hist_data + group_bin_boundaries_[group];
		const int num_bin = feature_groups_[group]->num_total_bin_;

		std::memset((void*)(data_ptr + 1), 0, (num_bin - 1) * sizeof(HistogramBinEntry));

		feature_groups_[group]->bin_data_->ConstructHistogram(data_indices, num_data, ptr_ordered_grad, data_ptr);

		for (int i = 0; i < num_bin; ++i) {

			data_ptr[i].sum_hessians = data_ptr[i].cnt * hessians[0];
		}


	}


}

void Dataset::FixHistogram(int feature_idx, double sum_gradient, double sum_hessian, int num_data,
							HistogramBinEntry* data) const {

	const int group = feature2group_[feature_idx];
	const int sub_feature = feature2subfeature_[feature_idx];
	const BinMapper* bin_mapper = feature_groups_[group]->bin_mappers_[sub_feature].get();
	const int default_bin = bin_mapper->GetDefaultBin();
	if (default_bin > 0) {
		const int num_bin = bin_mapper->num_bin();
		data[default_bin].sum_gradients = sum_gradient;
		data[default_bin].sum_hessians = sum_hessian;
		data[default_bin].cnt = num_data;
		for (int i = 0; i < num_bin; ++i) {
			if (i != default_bin) {
				data[default_bin].sum_gradients -= data[i].sum_gradients;
				data[default_bin].sum_hessians -= data[i].sum_hessians;
				data[default_bin].cnt -= data[i].cnt;
			}
		}
	}
}



}
