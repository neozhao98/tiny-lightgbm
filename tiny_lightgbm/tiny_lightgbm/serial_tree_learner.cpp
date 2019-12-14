

#include "tree_learner.h"
#include "config.h"

namespace Tiny_LightGBM {


void SerialTreeLearner::Init(const Dataset* train_data) {
	
	train_data_ = train_data;
	num_data_ = train_data_->num_data();
	//�������num total feature������ͬ�����������ǵļ򵥰���������ͬ��
	num_features_ = train_data_->num_features();

	//Ĭ����31
	//����
	int max_cache_size = Config::num_leaves;

	//��ͷϷ
	histogram_pool_.DynamicChangeSize(train_data_,max_cache_size);

	//���¶�����ϳ�ʼ����Ϊ�˷�����ʱ�������¼����Ϣ
	best_split_per_leaf_.resize(max_cache_size);
	train_data_->CreateOrderedBins(&ordered_bins_);
	for (int i = 0; i < static_cast<int>(ordered_bins_.size()); ++i) {
		if (ordered_bins_[i] != nullptr) {
			has_ordered_bin_ = true;
			break;
		}
	}
	smaller_leaf_splits_.reset(new LeafSplits(train_data_->num_data()));
	larger_leaf_splits_.reset(new LeafSplits(train_data_->num_data()));

	data_partition_.reset(new DataPartition(num_data_, max_cache_size));
	is_feature_used_.resize(num_features_);

	valid_feature_indices_ = train_data_->ValidFeatureIndices();

	ordered_gradients_.resize(num_data_);
	ordered_hessians_.resize(num_data_);

}

Tree* SerialTreeLearner::Train(const float* gradients, const float* hessians, bool is_constant_hessian) {

	gradients_ = gradients;
	hessians_ = hessians;
	is_constant_hessian_ = is_constant_hessian;

	//ÿ�������ѵ����31��Ҷ�ӣ����Σ�����ѵ��֮ǰ����Ҫ��һЩ��ʼ������Ϲ���
	BeforeTrain();

	//��ʼ��tree
	auto tree = std::unique_ptr<Tree>(new Tree(Config::num_leaves));

	//leaf��index��0��ʼ
	//������һ�����ѵļ��ɣ������ʼ�Ǹ��ڵ㣬Ҳ��Ҷ�ӣ�����index 0 ��
	//����һ�Σ����������µ�Ҷ�ӣ���Ҷ�Ӽ̳�0����Ҷ�ӱ�Ϊ1���Դ�����
	//��������ɣ�Ҷ�ӵ�index��0-30��31��Ҷ�ӣ�
	int left_leaf = 0;
	int cur_depth = 1;
	int right_leaf = -1;
	int init_splits = 0;
	
	//��ʼ���ѣ�������30�Σ��ͻ���31��Ҷ�ӣ�leaf-wise��
	for (int split = init_splits; split < Config::num_leaves; ++split) {

		//ÿ�η���Ҷ��֮ǰ�����
		if (BeforeFindBsetSplit(tree.get(), left_leaf, right_leaf)) {

			FindBestSplits();
		}
		int best_leaf = static_cast<int>(Utils::ArgMax(best_split_per_leaf_));
		const SplitInfo& best_leaf_SplitInfo = best_split_per_leaf_[best_leaf];

		if (best_leaf_SplitInfo.gain <= 0.0) {
			break;
		}

		Split(tree.get(), best_leaf, &left_leaf, &right_leaf);
		cur_depth = std::max(cur_depth, tree->leaf_depth(left_leaf));

	}
	return tree.release();
}

void SerialTreeLearner::Split(Tree* tree, int best_leaf, int* left_leaf, int* right_leaf) {

	const SplitInfo& best_split_info = best_split_per_leaf_[best_leaf];
	const int inner_feature_index = train_data_->InnerFeatureIndex(best_split_info.feature);

	//root������£���best_leaf = 0������leftleaf
	*left_leaf = best_leaf;

	auto threshold_double = train_data_->RealThreshold(inner_feature_index, best_split_info.threshold);
	*right_leaf = tree->Split(best_leaf,
								inner_feature_index,
								best_split_info.feature,
								best_split_info.threshold,
								threshold_double,
								static_cast<double>(best_split_info.left_output),
								static_cast<double>(best_split_info.right_output),
								static_cast<int>(best_split_info.left_count),
								static_cast<int>(best_split_info.right_count),
								static_cast<float>(best_split_info.gain),
								best_split_info.default_left);
	data_partition_->Split(best_leaf, train_data_, inner_feature_index,
							&best_split_info.threshold, 1, best_split_info.default_left, *right_leaf);


	auto p_left = smaller_leaf_splits_.get();
	auto p_right = larger_leaf_splits_.get();
	if (best_split_info.left_count < best_split_info.right_count) {
		smaller_leaf_splits_->Init(*left_leaf, data_partition_.get(), best_split_info.left_sum_gradient, best_split_info.left_sum_hessian);
		larger_leaf_splits_->Init(*right_leaf, data_partition_.get(), best_split_info.right_sum_gradient, best_split_info.right_sum_hessian);

	}
	else {
		smaller_leaf_splits_->Init(*right_leaf, data_partition_.get(), best_split_info.right_sum_gradient, best_split_info.right_sum_hessian);
		larger_leaf_splits_->Init(*left_leaf, data_partition_.get(), best_split_info.left_sum_gradient, best_split_info.left_sum_hessian);
		p_right = smaller_leaf_splits_.get();
		p_left = larger_leaf_splits_.get();
	}
	p_left->SetValueConstraint(best_split_info.min_constraint, best_split_info.max_constraint);
	p_right->SetValueConstraint(best_split_info.min_constraint, best_split_info.max_constraint);


}

void SerialTreeLearner::FindBestSplits() {

	std::vector<int> is_feature_used(num_features_, 0);

	for (int feature_index = 0;feature_index < num_features_; ++feature_index) {
		
		//if (parent_leaf_histogram_array_ != nullptr
		//	&& !parent_leaf_histogram_array_[feature_index].is_splittable()) {

		//	smaller_leaf_histogram_array_[feature_index].set_is_splittable(false);
		//	continue;
		//}

		//is_feature_used_ȫѡ
		is_feature_used[feature_index] = 1;
	}
	//���һ��ֻ�и��ڵ㡣use_subtract=false
	//�������use_subtract=true
	bool use_subtract = parent_leaf_histogram_array_ != nullptr;
	
	
	ConstructHistograms(is_feature_used, use_subtract);
	FindBestSplitsFromHistograms(is_feature_used, use_subtract);
}
void SerialTreeLearner::FindBestSplitsFromHistograms(const std::vector<int>& is_feature_used, bool use_subtract) {

	std::vector<SplitInfo> smaller_best(1);
	std::vector<SplitInfo> larger_best(1);
	//ԭʼ��feature����������group
	for (int feature_index = 0; feature_index < num_features_; ++feature_index) {


		SplitInfo smaller_split;

		//fixhistogram�Ǽ���ĳ��feature��default bin����0��
		//��ô��ʵ��value 0 ��Ӧ��binû�����������棬��ȥ��bin 0
		//��������Ϳ��Ի�ԭ���ݵ���ȷ��bin����ȥ

		//�����ص㣺
		//֮ǰ����histogram��ʱ���ǰ���group������ġ�
		//����������ѵ�ʱ���ǰ���ԭʼfeature�����ѵġ�
		//����fix����ԭʼfeature��fix
		//��֮ǰ�����bin0��bin1��bin2��....��bin10
		//���п���bin 0 ��Ĭ�ϣ�bin 1-5��feature1 �� ʣ�µ���feature2
		//���ʱ��feature1��default bin����bin3����ô����Ҫfix����Ϊbin 3����û���κ�����
		//fix��ʱ������Ҫ��bin 0- bin10��ȫ�����ݣ���ȥbin0 ��bin1��bin2��bin4��bin5.
		//��������֮�󣬶���feature1��˵����ʵ���Ѿ����������е�����

		//smaller_leaf_histogram_array_��������ʹ���һ��Ҷ����
		//smaller_leaf_histogram_array_[feature_index]�ͻ���feature����
		train_data_->FixHistogram(feature_index,
									smaller_leaf_splits_->sum_gradients(), smaller_leaf_splits_->sum_hessians(),
									smaller_leaf_splits_->num_data_in_leaf(),
									smaller_leaf_histogram_array_[feature_index].RawData());
		int real_fidx = train_data_->RealFeatureIndex(feature_index);
		//Ѱ����ѷ��ѵ�
		smaller_leaf_histogram_array_[feature_index].FindBestThreshold(smaller_leaf_splits_->sum_gradients(),
												smaller_leaf_splits_->sum_hessians(),
												smaller_leaf_splits_->num_data_in_leaf(),
												smaller_leaf_splits_->min_constraint(),
												smaller_leaf_splits_->max_constraint(),
												&smaller_split);

		smaller_split.feature = real_fidx;
		if (smaller_split > smaller_best[0]) {
			smaller_best[0] = smaller_split;
		}
		if (larger_leaf_splits_ == nullptr || larger_leaf_splits_->LeafIndex() < 0) { continue; }

		if (use_subtract) {
			larger_leaf_histogram_array_[feature_index].Subtract(smaller_leaf_histogram_array_[feature_index]);
		}
		else {
			train_data_->FixHistogram(feature_index, larger_leaf_splits_->sum_gradients(), larger_leaf_splits_->sum_hessians(),
				larger_leaf_splits_->num_data_in_leaf(),
				larger_leaf_histogram_array_[feature_index].RawData());
		}
		SplitInfo larger_split;
		// find best threshold for larger child
		larger_leaf_histogram_array_[feature_index].FindBestThreshold(
			larger_leaf_splits_->sum_gradients(),
			larger_leaf_splits_->sum_hessians(),
			larger_leaf_splits_->num_data_in_leaf(),
			larger_leaf_splits_->min_constraint(),
			larger_leaf_splits_->max_constraint(),
			&larger_split);
		larger_split.feature = real_fidx;
		if (larger_split > larger_best[0]) {
			larger_best[0] = larger_split;
		}


	}

	auto smaller_best_idx = Utils::ArgMax(smaller_best);
	int leaf = smaller_leaf_splits_->LeafIndex();
	best_split_per_leaf_[leaf] = smaller_best[smaller_best_idx];

	//���һ��ֻ�и��ڵ㡣��δ��벻��ִ��
	if (larger_leaf_splits_ != nullptr && larger_leaf_splits_->LeafIndex() >= 0) {
		leaf = larger_leaf_splits_->LeafIndex();
		auto larger_best_idx = Utils::ArgMax(larger_best);
		best_split_per_leaf_[leaf] = larger_best[larger_best_idx];
	}

}


void SerialTreeLearner::ConstructHistograms(const std::vector<int>& is_feature_used, bool use_subtract) {

	//���һ��Ϊ���ڵ�ִ��,���ڵ����smaller_leaf_histogram_array_
	//[0]�������Ǵ�feature0��ʼ����������histogram
	//ÿ��ֻ�ṹ��small��ߵ�histogram��largerֻ��Ҫ��������
	HistogramBinEntry* ptr_smaller_leaf_hist_data = smaller_leaf_histogram_array_[0].RawData() - 1;
	train_data_->ConstructHistograms(is_feature_used,
								smaller_leaf_splits_->data_indices(), smaller_leaf_splits_->num_data_in_leaf(),
								smaller_leaf_splits_->LeafIndex(),
								ordered_bins_, gradients_, hessians_,
								ordered_gradients_.data(), ordered_hessians_.data(), is_constant_hessian_,
								ptr_smaller_leaf_hist_data);

	//��Զ����ִ�У�������Ҫ���ӵ�Ϊlarger_leaf����histogram
	if (larger_leaf_histogram_array_ != nullptr && !use_subtract) {
		// construct larger leaf
		HistogramBinEntry* ptr_smaller_leaf_hist_data = smaller_leaf_histogram_array_[0].RawData() - 1;
		train_data_->ConstructHistograms(is_feature_used,
			smaller_leaf_splits_->data_indices(), smaller_leaf_splits_->num_data_in_leaf(),
			smaller_leaf_splits_->LeafIndex(),
			ordered_bins_, gradients_, hessians_,
			ordered_gradients_.data(), ordered_hessians_.data(), is_constant_hessian_,
			ptr_smaller_leaf_hist_data);

		if (larger_leaf_histogram_array_ != nullptr && !use_subtract) {
			// construct larger leaf
			HistogramBinEntry* ptr_larger_leaf_hist_data = larger_leaf_histogram_array_[0].RawData() - 1;
			train_data_->ConstructHistograms(is_feature_used,
				larger_leaf_splits_->data_indices(), larger_leaf_splits_->num_data_in_leaf(),
				larger_leaf_splits_->LeafIndex(),
				ordered_bins_, gradients_, hessians_,
				ordered_gradients_.data(), ordered_hessians_.data(), is_constant_hessian_,
				ptr_larger_leaf_hist_data);
		}
	}
}


bool SerialTreeLearner::BeforeFindBsetSplit(const Tree* tree, int left_leaf, int right_leaf) {

	//max_depthû������

	int num_data_in_left_child = GetGlobalDataCountInLeaf(left_leaf);
	int num_data_in_right_child = GetGlobalDataCountInLeaf(right_leaf);

	//���Ҷ�ӻ��������֣����Ҷ���ϵ����������Ѿ������ˣ�������
	if (num_data_in_left_child < static_cast<int>(Config::min_data_in_leaf * 2)
		&& num_data_in_right_child < static_cast<int>(Config::min_data_in_leaf * 2)) {

		best_split_per_leaf_[left_leaf].gain = -std::numeric_limits<float>::infinity();
		if (right_leaf > 0) {
			best_split_per_leaf_[right_leaf].gain = -std::numeric_limits<float>::infinity();
		}
		return false;

	}

	if (right_leaf < 0) {
		//�ʼֻ�и��ڵ㣬��ȻҪ���ѣ��ͰѸ��ڵ����smaller_leaf_histogram_array_
		histogram_pool_.Get(left_leaf, &smaller_leaf_histogram_array_);
		larger_leaf_histogram_array_ = nullptr;
	}
	else if (num_data_in_left_child < num_data_in_right_child) {

		// ͨ��swap������ʵ�����������Ľ���
		//������Ҫ��������Ҷ��Ҫ�и�˳�򣬼���Ҷ������Ҫ��һЩ��Ĭ�ϸ��ڵ�Ҳ����Ҷ�ӣ�˳����£�
		//��Ȼ��Ҷ�Ӿ�������larger_leaf_histogram_array_
		if (histogram_pool_.Get(left_leaf , &larger_leaf_histogram_array_)) {
			parent_leaf_histogram_array_ = larger_leaf_histogram_array_;
		}
		histogram_pool_.Move(left_leaf, right_leaf);
		histogram_pool_.Get(left_leaf, &smaller_leaf_histogram_array_);
	}
	else {
		if (histogram_pool_.Get(left_leaf, &larger_leaf_histogram_array_)) { 
			parent_leaf_histogram_array_ = larger_leaf_histogram_array_; 
		}
		histogram_pool_.Get(right_leaf, &smaller_leaf_histogram_array_);
	}

	return true;



}

void SerialTreeLearner::BeforeTrain() {

	histogram_pool_.ResetMap();

	for (int i = 0; i < num_features_; ++i) {
		is_feature_used_[i] = 1;

	}
	data_partition_->Init();


	for (int i = 0; i < Config::num_leaves; ++i) {
		best_split_per_leaf_[i].Reset();
	}

	smaller_leaf_splits_->Init(0,data_partition_.get(), gradients_, hessians_);
	larger_leaf_splits_->Init();
}



}