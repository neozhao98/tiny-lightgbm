#pragma once

#include "bin.h"
#include "split_info.hpp"
#include  "utils.h"
#include "config.h"

namespace Tiny_LightGBM {

class FeatureMetainfo {

public:
	int num_bin;
	int default_bin;

	int bias = 0;
};

class FeatureHistogram {
public:
	void Init(HistogramBinEntry* data, const FeatureMetainfo* meta) {
		meta_ = meta;
		data_ = data;

		find_best_threshold_fun_ = std::bind(&FeatureHistogram::FindBestThresholdNumerical , this ,
											std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, 
											std::placeholders::_4, std::placeholders::_5, std::placeholders::_6);

	}

	static double ThresholdL1(double s, double l1) {
		const double reg_s = std::max(0.0, std::fabs(s) - l1);
		return Utils::Sign(s) *reg_s;
	}

	static double CalculateSplittedLeafOutput(double sum_gradients, double sum_hessians, double l1, double l2, double max_delta_step) {
		double ret = -ThresholdL1(sum_gradients, l1) / (sum_hessians + l2);
		return ret;
	}

	/*ʵ������������������Ҷ�ӵ������
	Ҷ�ӵ���������Ǽ򵥵Ľ��ڵ��ϵ�ֵ��һ��ƽ������xgboost����
	lightgbm�Ŀ���£�ÿ��Ҷ�ӵ������ w ��ȷ���ģ�����ѵ�ȡֵ��
	����ѵ���ʧ����������ÿ��Ҷ�ӵ���ѻ���ʧ�����ġ�
	����������£�Ҷ�ӵ������sum_gradient / ��sum_hessian + ����
	��Ҳ������Ϊʲô�������ź�����ԭ�򡣽�Ҷ������������ţ�
	���صļ������ⰲ��
	*/
	static double CalculateSplittedLeafOutput(double sum_gradients, double sum_hessians, double l1, double l2, double max_delta_step,
		double min_constraint, double max_constraint) {
		double ret = CalculateSplittedLeafOutput(sum_gradients, sum_hessians, l1, l2, max_delta_step);
		if (ret < min_constraint) {
			ret = min_constraint;
		}
		else if (ret > max_constraint) {
			ret = max_constraint;
		}
		return ret;
	}
	static double GetLeafSplitGain(double sum_gradients, double sum_hessians, double l1, double l2, double max_delta_step) {

		double output = CalculateSplittedLeafOutput(sum_gradients, sum_hessians, l1, l2, max_delta_step);
		return GetLeafSplitGainGivenOutput(sum_gradients, sum_hessians, l1, l2, output);

	}
	void Subtract(const FeatureHistogram& other) {
		for (int i = 0; i < meta_->num_bin - meta_->bias; ++i) {
			data_[i].cnt -= other.data_[i].cnt;
			data_[i].sum_gradients -= other.data_[i].sum_gradients;
			data_[i].sum_hessians -= other.data_[i].sum_hessians;
		}
	}

	static double GetLeafSplitGainGivenOutput(double sum_gradients, double sum_hessians, double l1, double l2, double output) {

		const double sg_l1 = ThresholdL1(sum_gradients, l1);
		return -(2.0 * sg_l1 * output + (sum_hessians + l2)*output*output);
	}

	static double GetSplitGains(double sum_left_gradients, double sum_left_hessians,
								double sum_right_gradients, double sum_right_hessians,
								double l1, double l2, double max_delta_step,
								double min_constraint, double max_constraint, int monotone_constaint) {

		double left_output = CalculateSplittedLeafOutput(sum_left_gradients, sum_left_hessians, l1, l2, max_delta_step ,  min_constraint, max_constraint);
		double right_output = CalculateSplittedLeafOutput(sum_right_gradients, sum_right_hessians, l1, l2, max_delta_step, min_constraint, max_constraint);

		return GetLeafSplitGainGivenOutput(sum_left_gradients, sum_left_hessians, l1, l2, left_output)
			+ GetLeafSplitGainGivenOutput(sum_right_gradients, sum_right_hessians, l1, l2, right_output);

	}

	void FindBsetThresholdSequence(double sum_gradient , double sum_hessian , int num_data ,
									double min_constraint , double max_constraint,
									double min_gain_shift , SplitInfo* output , 
									int dir , bool skip_default_bin , bool use_na_sa_missing) {

		const int bias = meta_->bias;
		double best_sum_left_gradient = NAN;
		double best_sum_left_hessian = NAN;
		double best_gain = -std::numeric_limits<float>::infinity();

		int best_left_count = 0;
		int best_threshold = meta_->num_bin;

		//��ʼ���ҵ���,dir����
		double sum_right_gradient = 0.0f;
		double sum_right_hessian = 1e-15f;
		int right_count = 0;

		int t = meta_->num_bin - 1 - bias - use_na_sa_missing;
		int t_end = 1 - bias;

		for (; t >= t_end; --t) {

			sum_right_gradient += data_[t].sum_gradients;
			sum_right_hessian += data_[t].sum_hessians;
			right_count += data_[t].cnt;

			//1e-3�ǳ��Σ���min_sum_hessian_in_leaf
			if (right_count < Config::min_data_in_leaf || sum_right_hessian < 1e-3) continue;
			int left_count = num_data - right_count;

			if (left_count < Config::min_data_in_leaf) break;

			double sum_left_hessian = sum_hessian - sum_right_hessian;

			if (sum_left_hessian < 1e-3) break;

			double sum_left_gradient = sum_gradient - sum_right_gradient;

			double current_gain = GetSplitGains(sum_left_gradient, sum_left_hessian, sum_right_gradient, sum_right_hessian,
												0.0, 0.0, 0.0,
												min_constraint, max_constraint, 0.0);
			if (current_gain <= min_gain_shift) continue;

			is_splittable_ = true;
			if (current_gain > best_gain) {
				best_left_count = left_count;
				best_sum_left_gradient = sum_left_gradient;
				best_sum_left_hessian = sum_left_hessian;

				best_threshold = static_cast<int>(t - 1 + bias);
				best_gain = current_gain;

			}

		}
		if (is_splittable_ && best_gain > output->gain) {
			output->threshold = best_threshold;
			output->left_output = CalculateSplittedLeafOutput(best_sum_left_gradient, best_sum_left_hessian,
									0.0, 0.0, 0.0,
									min_constraint, max_constraint);
			output->left_count = best_left_count;
			output->left_sum_gradient = best_sum_left_gradient;
			output->left_sum_hessian = best_sum_left_hessian - 1e-15f;
			output->right_output = CalculateSplittedLeafOutput(sum_gradient - best_sum_left_gradient,
								sum_hessian - best_sum_left_hessian,
								0.0,0.0,0.0,
								min_constraint, max_constraint);
			output->right_count = num_data - best_left_count;
			output->right_sum_gradient = sum_gradient - best_sum_left_gradient;
			output->right_sum_hessian = sum_hessian - best_sum_left_hessian - 1e-15f;
			output->gain = best_gain;
			output->default_left = dir == -1;
		}


	}

	


	void FindBestThresholdNumerical(double sum_gradient , double sum_hessian , 
									int num_data , double min_constraint , 
									double max_constraint , SplitInfo* output) {

		is_splittable_ = false;
		//�����ֱ���l1 �� l2 �� max_delta_step
		//�������֮ǰ����ֵ
		double gain_shift = GetLeafSplitGain(sum_gradient, sum_hessian, 0.0, 0.0, 0.0);
		//double min_gain_to_split = 0.0;
		double min_gain_shift = gain_shift + 0.0;

		//���ﲻ������ֵ����Ȼ�Ļ�Ӧ������������ɨ�裬����ֵ�ŵ��������߶Ա�Ч��
		//��lightgbm������ֵ�ķ����ǣ�����ֵ�ŵ��������߶����ԣ�����ô��
		FindBsetThresholdSequence(sum_gradient, sum_hessian, num_data, min_constraint, max_constraint, min_gain_shift, output, -1, false, false);


	}


	bool is_splittable() { return is_splittable_; }
	void set_is_splittable(bool val) { is_splittable_ = val; }

	HistogramBinEntry* RawData() {

		return data_;
	}


	void FindBestThreshold(double sum_gradient, double sum_hessian, int num_data, double min_constraint, double max_constraint,
							SplitInfo* output) {
		output->default_left = true;
		output->gain = -std::numeric_limits<float>::infinity();
		find_best_threshold_fun_(sum_gradient, sum_hessian + 2 * 1e-15f, num_data, min_constraint, max_constraint, output);

		// 1ʵ������feature��penalty
		output->gain *= 1;
	}



private:

	bool is_splittable_;
	const FeatureMetainfo* meta_;
	HistogramBinEntry* data_;

	std::function<void(double, double, int, double, double, SplitInfo*)> find_best_threshold_fun_;
};



class HistogramPool {
public:
	HistogramPool() {
		cache_size_ = 0;
		total_size_ = 0;
	}


	//HistogramPool������Ҫ��
	//������ά��һ��pool_������
	//std::vector<std::unique_ptr<FeatureHistogram[]>> pool_;
	//���Ǻ��ĵĲ�����pool[i][j]��i����Ҷ�ӣ�j����feature
	void DynamicChangeSize(const Dataset* train_data, int cache_size) {
		if (feature_metas_.empty()) {
			int num_feature = train_data->num_features();
			feature_metas_.resize(num_feature);

			//�����feature����EFB֮���feature������ԭʼ��feature
			//�и���Ҫ�ĵ㣺
			//�����ǹ�����feature group
			//��������������ѵ����ȥѰ��threshold��ʱ��
			//���Ǹ���ԭʼ��feature1ȥ��
			for (int i = 0; i < num_feature; ++i) {

				//pool_[i][j]ʵ�����������࣬һ����HistogramBinEntry������bin����Ϣ
				//һ����FeatureMetaInfo������feature��һЩ��Ϣ
				//���¾��ǳ�ʼ��FeatureMetaInfo
				feature_metas_[i].num_bin = train_data->FeatureNumBin(i);
				feature_metas_[i].default_bin = train_data->FeatureBinMapper(i)->GetDefaultBin();
				if (train_data->FeatureBinMapper(i)->GetDefaultBin() == 0) {
					feature_metas_[i].bias = 1;
				}
				else {
					feature_metas_[i].bias = 0;
				}
			}
		}
		int num_total_bin = train_data->NumTotalBin();
		int old_cache_size = static_cast<int>(pool_.size());

		if (cache_size > old_cache_size) {
			pool_.resize(cache_size);
			data_.resize(cache_size);
		}
		
		for (int i = old_cache_size; i < cache_size; ++i) {

			pool_[i].reset(new FeatureHistogram[train_data->num_features()]);
			data_[i].resize(num_total_bin);
			int offset = 0;

			//���³�ʼ���ǳ��ؼ�
			for (int j = 0; j < train_data->num_features(); ++j) {
				
				//offsetsʵ�����ǰ�����feature������bin������һ����
				//����CHECK(offset == num_total_bin);
				//Ĭ��feature��subfeature����0����Ϊû�ж��feature���group
				//offsets+=1
				offset += static_cast<int>(train_data->SubFeatureBinOffset(j));

				//������ǳ�ʼHistogramBinEntry����FeatureMetaInfo
				//��������Щ������ÿһ��Ҷ�ӵ�ÿһ��feature����ȥ��
				//���Ұ���һ������find_best_threshold_fun_�����ĺ�����Ѱ��threshold

	
				//����ÿ��Ҷ��i��˵����ÿһ��ԭʼfeature j��HistogramBinEntry��λ�ö�������ȷ����
				//HistogramBinEntry�����һ��feature��һ��bin���Ե�
				//һ��feature�ж��HistogramBinEntry������m������ͬʱһ��Ҷ�����ж��feature������n����
				//������31��Ҷ��
				//std::vector<std::vector<HistogramBinEntry>> data_;
				//data_[i]����ĳһ��Ҷ���ϣ�����feature��m*n��HistogramBinEntry
				//��Ȼn��m���Ƕ�ֵ
				//����ĳ��Ҷ�ӱ���0����ôdata_[0]�ʹ������е�bin��ͬʱdeta_[0][0]����Ҷ��0��feature0��bin����ʼ
				pool_[i][j].Init(data_[i].data() + offset, &feature_metas_[j]);

				//���м�һ�Ĳ���
				//��Ϊnum_bin����ÿһ��ԭʼfeature��bin����
				//���������num_total_bin��feature group�µ�bin������Ҫת������һ��
				auto num_bin = train_data->FeatureNumBin[j];
				if (train_data->FeatureBinMapper(j)->GetDefaultBin() == 0) {
					num_bin -= 1;
				}
				offset += static_cast<int>(num_bin);
			}
		}
	}

	void ResetMap() {


		

	}
	bool Get(int idx, FeatureHistogram** out) {
		*out = pool_[idx].get();
		return true;
	}

	void Move(int src_idx, int dst_idx) {

		std::swap(pool_[src_idx], pool_[dst_idx]);
		return;

	}

private:
	int cache_size_;
	int total_size_;

	std::vector<FeatureMetainfo> feature_metas_;

	std::vector<std::unique_ptr<FeatureHistogram[]>> pool_;

	std::vector<std::vector<HistogramBinEntry>> data_;

};


}