#pragma once

// std Include
#include <fstream>
#include <omp.h>

// vMIX Includes
#include "Mem.hpp"
#include "Charset.hpp"

using namespace std;

class IODevice {
protected:
	// Members
	const uint blockSize, numBlocks;
	uint curPage, busyUntil;
	vWord *data;

	// Helper
	inline void cpyPage(vWord *dst, const uint page) const {
		memcpy(dst, &data[(page * blockSize)], (blockSize * sizeof(vWord)));
	};
	inline void cpyPage(const uint page, const vWord *src) const {
		memcpy(&data[(page * blockSize)], src, (blockSize * sizeof(vWord)));
	};

	inline uint currentTimeMilli() const {
		return uint(omp_get_wtime() * 1000.);
	};

public:
	// Constructor
	IODevice(const uint _blockSize, const uint _numBlocks) 
		: blockSize{_blockSize}, numBlocks{_numBlocks}, 
		  curPage{0}, busyUntil{0}, data{ new vWord[blockSize * numBlocks]{} } {};
	// Destructor
	~IODevice() {
		delete [] data;
	};

	// Interface
	virtual bool in(vWord *addr) {
		return false;
	};
	virtual bool out(const vWord *addr) {
		return false;
	};
	virtual bool ioc(const uint m) {
		return false;
	};
	virtual bool busy() const {
		return currentTimeMilli() < busyUntil;
	};
};

class IOTape : public IODevice {
private:
	// Members
	const string path;

public:
	// Constructor
	IOTape(const string _path) : IODevice(100, 2000), path(_path) {
		ifstream inFile(path, ios::binary | ios::in);
		if (inFile.is_open()) {
			inFile.read((char*) data, (blockSize * numBlocks * sizeof(vWord)));
			inFile.close();
		}
	};

	// Interface
	bool in(vWord *addr) {
		busyUntil = currentTimeMilli() + (blockSize * 100);
		cpyPage(addr, curPage);
		curPage++;
		return true;
	};
	bool out(const vWord *addr) {
		busyUntil = currentTimeMilli() + (blockSize * 150);
		cpyPage(curPage, addr);
		ofstream outFile(path, ios::binary | ios::out);
		if (outFile.is_open()) {
			outFile.write((char*) data, (blockSize * numBlocks * sizeof(vWord)));
			outFile.close();
		}
		curPage++;
		return true;	
	};
};

class IODisk : public IODevice {
private:
	// Members
	const string path;

public:
	// Constructor
	IODisk(const string &_path) : IODevice(100, 4000), path(_path) {
		ifstream inFile(path, ios::binary | ios::in);
		if (inFile.is_open()) {
			inFile.read((char*) data, (blockSize * numBlocks * sizeof(vWord)));
			inFile.close();
		}
	};

	// Interface
	bool in(vWord *addr) {
		busyUntil = currentTimeMilli() + (blockSize * 100);
		cpyPage(addr, curPage);
		return true;
	};
	bool out(const vWord *addr) {
		busyUntil = currentTimeMilli() + (blockSize * 150);
		cpyPage(curPage, addr);
		ofstream outFile(path, ios::binary | ios::out);
		if (outFile.is_open()) {
			outFile.write((char*) data, (blockSize * numBlocks * sizeof(vWord)));
			outFile.close();
		}
		return true;
	};
};

class IOCardReader : public IODevice {
private:
	// Members
	const string dir;
	
public:
	// Constructor
	IOCardReader(const string &path) : IODevice(16, 1), dir(path) {};

	// Interface
	bool in(vWord *addr) {
		busyUntil = currentTimeMilli() + (blockSize * 150);
		ifstream inFile(dir + to_string(curPage) + ".card", ios::in | ios::binary);
		if (inFile.is_open()) {
			for (int i = 0; i < blockSize; ++i) {
				vWord &val = *addr++;
				val.sign = P;
				val.data = 0;
				for (int j = 0; j < 5; ++j) {
					char tmp{};
					inFile.read(&tmp, 1);
					uint v = toVal(tmp);
					if (v == (uint) CHAR_INDEX::NULLCHAR) {
						v = (uint) CHAR_INDEX::SPACE;
					}
					val.data |= (v & BYTE_MASK) << (j * 6);

					if (j > 0 && (j % 4) == 0) {
						inFile.read(&tmp, 1);
					}
				}

			}
			inFile.close();
		}
		curPage++;
		return true;
	};
};

class IOCardWriter : public IODevice {
private:
	// Members
	const string dir;
	
public:
	// Constructor
	IOCardWriter(const string &path) : IODevice(16, 1), dir(path) {};

	// Interface
	bool out(const vWord *addr) {
		busyUntil = currentTimeMilli() + (blockSize * 250);
		ofstream outFile(dir + to_string(curPage) + ".card", ios::out);
		if (outFile.is_open()) {
			for (int i = 0; i < numBlocks; ++i) {
				for (int j = 0; j < blockSize; ++j) {
					const uint val = *addr++;
					for (int k = 4; k >= 0; -k) {
						outFile << toChar((val >> (k * 6)) & BYTE_MASK);
					}

					if (j > 0 && (j % 4) == 0) {
						outFile << '\n';
					}
				}
			}
			outFile.close();
		}
		curPage++;
		return true;
	};
};

class IOLinePrinter : public IODevice {
private:
	// Members
	ostream outStream;

public:
	// Constructor
	IOLinePrinter(ostream &_out) : IODevice(24, 1), outStream(_out.rdbuf()) {};

	// Interface
	bool out(const vWord *addr) {
		busyUntil = currentTimeMilli() + (blockSize * 100);
		for (int i = 0; i < blockSize; ++i) {
			const uint val = *addr++;
			for (int j = 4; j >= 0; --j) {
				outStream << toChar((val >> (j * 6)) & BYTE_MASK);
			}
		}
		outStream << '\n';
		return true;
	};
};

class IOTypeWriter : public IODevice {
private:
	// Members
	istream inStream;

public:
	// Constructor
	IOTypeWriter(istream &_in) : IODevice(14, 1), inStream(_in.rdbuf()) {};

	// Interface
	bool in(vWord *addr) {
		bool esc = false;
		for (int i = 0; !esc && i < blockSize; ++i) {
			vWord &val = *addr++;
			val = vWord{};
			for (int j = 4; j >= 0; --j) {
				char tmp{};
				inStream.get(tmp);
				uint v = toVal(tmp);
				if (v == (uint) CHAR_INDEX::NULLCHAR) {
					v = (uint) CHAR_INDEX::SPACE;
					
					val.data |= (v & BYTE_MASK) << (j * 6);
					for (int j = i + 1; j < blockSize; ++j) {
						*addr++ = vWord{};
					}
					esc = true;
					break;
				}
				val.data |= (v & BYTE_MASK) << (j * 6);
			}
		}
		return true;
	};
};

class IOPaperTape : public IODevice {
private:
	// Members
	const string path;

public:
	// Constructor
	IOPaperTape(const string &_path) : IODevice(14,1000), path(_path) {};

	// Interface
	bool out(const vWord *addr) {
		busyUntil = currentTimeMilli() + (blockSize * 200);
		ofstream outFile(path, ios::out);
		if (outFile.is_open()) {
			for (int i = 0; i < numBlocks; ++i) {
				for (int j = 0; j < blockSize; ++j) {
					const uint val = *addr++;
					for (int k = 0; k < 5; ++k) {
						outFile << toChar((val << (k * 6)) & BYTE_MASK);
					}
				}
			}
			outFile.close();
		}
		return true;
	};
};