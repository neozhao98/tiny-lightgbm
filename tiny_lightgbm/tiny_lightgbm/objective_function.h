#pragma once

#include "dataset.h"
#include <functional>



namespace Tiny_LightGBM {

class ObjectiveFunction {

public:

	virtual ~ObjectiveFunction() {}

	static ObjectiveFunction* CreateObjectiveFunction();

	virtual void Init(const Metadata& metadata, int num_data) = 0;

	virtual void GetGradients(const double* score, float* graddients, float* hessians)const =0;


};


class RegressionL2loss :public ObjectiveFunction {

public:
	void Init(const Metadata& metadata, int num_data) override {};

	void GetGradients(const double* score, float* gradients, float* hessians) const override{

		for (int i = 0; i < num_data_; ++i) {
			//label_����仯��score�᲻�ϸ���Ϊ���µ�score
			gradients[i] = static_cast<float>(score[i] - label_[i]);
			hessians[i] = 1.0f;
		}

	}


//�ṩ��������
protected:
	int num_data_;
	const float* label_;

};








}
