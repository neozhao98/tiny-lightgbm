#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <memory>


namespace Tiny_LightGBM{

//�������в���������python�ӿ��в��ṩ����ѡ�
//���е��޸Ķ��������ļ�config.h�н���
struct Config {

public:

	static const int max_bin = 255;

	static const int min_data_in_bin = 3;

	

	static const int num_leaves = 31;

	//�����������Ҷ�������� �� Ĭ��û�����ƣ�Ϊ0
	static const double max_delta_step;

	//������������0��Ĭ��������
	//l1,l2�ֱ��Ӧgradient��hessian��Լ��
	static const double lambda_l1;
	static const double lambda_l2;


	double min_gain_to_split = 0.0;

	static const int min_data_in_leaf = 20;

};


}
