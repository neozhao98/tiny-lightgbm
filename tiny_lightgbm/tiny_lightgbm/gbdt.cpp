

#include "gbdt.h"
#include "boosting.h"
#include "tree_learner.h"


namespace Tiny_LightGBM {

void GBDT::Init(const Dataset* train_data,
	const ObjectiveFunction* objective_function,
	const std::vector<const Metric*>& training_metrics) {

	//��ϳ�ʼ��
	train_data_ = train_data;
	iter_ = 0;
	objective_function_ = objective_function;
	//��������Ǵ���ÿһ��epoch���棬��ѵ�����ٿ���
	//lightgbm�������ࣨ���Ƕ��ǩ������ʱ��������5��
	//��ôÿ��epoch�ͻṹ��5������ÿ������Ӧһ�����ʵ�ֶ�����
	//�������softmax���Եõ�����֮��ģ�����Ҳ�ǻع�����
	num_tree_per_iteration_ = 1;
	//��ȻL2loss���棬���׵�����
	is_constant_hessian_ = true;

	//��ͷϷ
	tree_learner_ = std::unique_ptr<TreeLearner>(TreeLearner::CreateTreeLearner());
	tree_learner_->Init(train_data_);


	//���¶�����ϳ�ʼ��
	training_metrics_.clear();
	for (const auto& metric : training_metrics) {
		training_metrics_.push_back(metric);
	}
	training_metrics_.shrink_to_fit();

	//�ʼ���е�data score����0
	train_score_updater_.reset(new ScoreUpdater(train_data_, num_tree_per_iteration_));

	num_data_ = train_data_->num_data();

	int total_size = static_cast<int>(num_data_)*num_tree_per_iteration_;
	gradients_.resize(total_size);
	hessians_.resize(total_size);

	bag_data_cnt_ = num_data_;
	bag_data_indices_.clear();
	tmp_indices_.clear();
	is_use_subset_ = false;

	class_need_train_ = std::vector<bool>(num_tree_per_iteration_, true);

	//Ĭ�ϲ�����shrinkage
	shrinkage_rate_ = 1.0f;

	//���������ԭʼfeature��������EFB֮���
	max_feature_idx_ = 0;
	max_feature_idx_ = train_data_->num_total_features() - 1;


}

void GBDT::UpdateScore(const Tree* tree, const int cur_tree_id) {

	//����
	train_score_updater_->AddScore(tree_learner_.get(), tree, cur_tree_id);

}

//��ʼѵ�������ĺ���
bool GBDT::TrainOneIter(const float* gradients, const float* hessians) {

	// 0.0 Ĭ�Ͼ���double ������Ҫ����ת����0.0fĬ�Ͼ���float
	//��ʵinitscore������label��ƽ��ֵ������ѵ�����������
	std::vector<double> init_scores(num_tree_per_iteration_, 0.0);

	//���㵱ǰ��һ���׵�
	//��Ȼÿ��epoch��һ���׵�����һ������Ϊ�������Ϊ
	//ÿ��epoch�ĳ�ʼֵ��������ʵlabelԽ��Խ��
	Boosting();
	gradients = gradients_.data();
	hessians = hessians_.data();

	//ʡ��bagging���������е�baggin��û������Ĭ��ʹ��ȫ�����ݣ�ȫ��feature

	bool should_continue = false;
	for (int cur_tree_id = 0; cur_tree_id < num_tree_per_iteration_; ++cur_tree_id){
		const int bias = cur_tree_id * num_data_;

		//ÿ��epoch������num_tree_per_iteration_��ô�����
		std::unique_ptr<Tree> new_tree(new Tree(2));
		
		auto grad = gradients + bias;
		auto hess = hessians + bias;
		//���ĺ�������ʼѵ��һ����
		new_tree.reset(tree_learner_->Train(grad, hess, is_constant_hessian_));

		//ѵ����ϣ����������ѣ���ô��ӵ�ģ�ͣ����������ݼ�¼
		if (new_tree->num_leaves() > 1) {
			should_continue = true;
			new_tree->Shrinkage(shrinkage_rate_);
			//���score����֮�󣬾ͻ��õ���һ�εļ���һ���׵�
			UpdateScore(new_tree.get(), cur_tree_id);
		}
		//��֮����������������ֹͣ���к�����ѵ����������ֵΪ0������
		else {
			if (models_.size() < static_cast<size_t>(num_tree_per_iteration_)) {
				double output = 0.0;
				output = init_scores[cur_tree_id];
				new_tree->AsConstantTree(output);
				train_score_updater_->AddScore(output, cur_tree_id);
			}

		}

		models_.push_back(std::move(new_tree));
	}

	if (!should_continue) {
		if (models_.size() > static_cast<size_t>(num_tree_per_iteration_)) {
			for (int cur_tree_id = 0; cur_tree_id < num_tree_per_iteration_; ++cur_tree_id) {
				//���һ����û���ã��߳�
				models_.pop_back();
			}
		}
		return true;
	}

	++iter_;
	return false;


}

void GBDT::Boosting() {

	int num_score = 0;

	//����һ���׵�
	objective_function_->GetGradients(GetTrainingScore(&num_score), gradients_.data(), hessians_.data());

}

const double* GBDT::GetTrainingScore(int* out_len) {

	return train_score_updater_->score();


}


double GBDT::BoostFromAverage(int class_id, bool update_scorer) {

	if (models_.empty() && !train_score_updater_->has_init_score()) {

		double init_score = 0.0;
	}
	return 0.0f;
}


void GBDT::Predict(const double* features, double* output) const {

	PredictRaw(features, output);
	
}

void GBDT::PredictRaw(const double* features, double* output) const {

	std::memset(output, 0, sizeof(double) * num_tree_per_iteration_);

	for (int i = 0; i < num_iteration_for_pred_; ++i) {
		// predict all the trees for one iteration
		for (int k = 0; k < num_tree_per_iteration_; ++k) {
			output[k] += models_[i * num_tree_per_iteration_ + k]->Predict(features);
		}
		
	}
}


}