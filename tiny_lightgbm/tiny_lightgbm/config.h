#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <memory>


namespace Tiny_LightGBM{

//保存所有参数，即在python接口中不提供参数选项。
//所有的修改都在配置文件config.h中进行
struct Config {

public:

	static const int max_bin = 255;

	static const int min_data_in_bin = 3;

	

	static const int num_leaves = 31;

	//这个参数控制叶子最大输出 ， 默认没有限制，为0
	static const double max_delta_step;

	//以下两个都是0，默认无限制
	//l1,l2分别对应gradient和hessian的约束
	static const double lambda_l1;
	static const double lambda_l2;


	double min_gain_to_split = 0.0;

	static const int min_data_in_leaf = 20;

};


}
