#pragma once

#include <vector>
#include <functional>
#include <unordered_map>
#include <sstream>
namespace Tiny_LightGBM {



struct  HistogramBinEntry{
public:
	double sum_gradients = 0.0f;
	double sum_hessians = 0.0f;
	int cnt = 0;
};


//ʵ�ֽ�ԭʼfeature�ֲ�����װͰ��bins = 255
//BinMapper����Ե���ԭʼfeature��
class BinMapper {

public:
	BinMapper();
	~BinMapper();

	//BinMapper�ĺ��ĺ���������bin
	void FindBin(double* values, int num_values, int num_row);

	inline int num_bin() const { return num_bin_; }

	inline int ValueToBin(double value) const;

	inline int GetDefaultBin() const {
		return default_bin_;
	}

	inline double BinToValue(uint32_t bin) const {
		
		//Ĭ������numerical�������
		return bin_upper_bound_[bin];
		
	}
	
private:
	int num_bin_;
	double min_val_;
	double max_val_;
	int default_bin_;
	std::vector<double> bin_upper_bound_;

};

inline int BinMapper::ValueToBin(double value) const {
	int l = 0;
	int r = num_bin_ - 1;
	while (l < r) {
		int m = (r + l - 1) / 2;
		if (value <= bin_upper_bound_[m]) {
			r = m;
		}
		else {
			l = m + 1;
		}
	}
	return l;

}

class OrderedBin {

public:
private:

};


class Bin {

public:
	static Bin* CreateBin(int num_data, int num_bin);


	virtual void Push(int idx, int value) = 0;

	virtual OrderedBin* CreateOrderedBin() const = 0;
	virtual void ConstructHistogram(const int* data_indices, int num_data, const float* ordered_gradients, HistogramBinEntry* out) const;
private:


};




class DenseBin :public Bin {
public:
	//DenseBin��ʼ���ǳ���Ҫ�ĵط������ǰ�data_���������ݶ���ʼ��Ϊ0
	//data_��һ��vector����index�������ݵ�index��value����ĳ��feature��bin
	//������featureĬ�϶��Ƿ���bin0����ط�
	DenseBin(int num_data):num_data_(num_data),data_(num_data_ , 0){}


	void Push(int idx, int value) override{
		data_[idx] = value;
	}

	//������˵��dense������¾Ͳ���Ҫ������
	OrderedBin* CreateOrderedBin() const override { return nullptr; }

	void ConstructHistogram(const int* data_indices, int num_data, const float* ordered_gradients, HistogramBinEntry* out) const override;

	int Split(
		uint32_t min_bin, uint32_t max_bin, uint32_t default_bin, bool default_left,
		uint32_t threshold, int* data_indices, int num_data,
		int* lte_indices, int* gt_indices) const;


private:

	int num_data_;
	std::vector<double> data_;
};



}
