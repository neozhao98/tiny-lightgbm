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

};


}
