#pragma once

#include <Windows.h>
#include <utility>
#include <stdexcept>


struct ErrorHandler
{
public:
	HRESULT err = 0;
	bool wasTripped = false;
	bool printErrors = true;

	ErrorHandler() = default;

	ErrorHandler& operator=(const HRESULT errH);
};

/* Simple error checker
LPVOID lpMsgBuf;
DWORD dw = GetLastError(); 

if (FormatMessage(
FORMAT_MESSAGE_ALLOCATE_BUFFER | 
FORMAT_MESSAGE_FROM_SYSTEM |
FORMAT_MESSAGE_IGNORE_INSERTS,
NULL,
dw,
MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
(LPTSTR) &lpMsgBuf,
0, NULL) == 0) {
MessageBox(NULL, TEXT("FormatMessage failed"), TEXT("Error"), MB_OK);
}

MessageBox(NULL, (LPCTSTR)lpMsgBuf, TEXT("Error"), MB_OK);

LocalFree(lpMsgBuf);
*/

//pipe queue, thread safe implementation of a queue for one reader and one writer
template <typename T>
class _PipeQRegion
{
public:
	size_t size = 0;
	T* data = nullptr;
	_PipeQRegion* next = nullptr;

	_PipeQRegion() {}
	_PipeQRegion(size_t sizeT) { size = sizeT; data = reinterpret_cast<T*>(new char[size*sizeof(T)]); }

	~_PipeQRegion()
	{ 
		if (data != nullptr)
			delete[] reinterpret_cast<char*>(data); 
		//if (next != nullptr) next is managed by PipeQ
		//	delete next;
		data = nullptr; 
		size = 0;
		next = nullptr;
	}
};

template<typename T>
class PipeQ //could optimize by keeping track of last region and reusing it if it is empty when a new region is needed
{
private:
	_PipeQRegion<T>* head = nullptr;
	_PipeQRegion<T>* tail = nullptr;
	size_t pushOff = 0;
	size_t popOff = 0;
public:
	int newRegionSizes = 32;

	PipeQ() { }

	void push_back(T&& mov)
	{
		if (head == nullptr) //check if this queue is empty
		{
			head = new _PipeQRegion<T>(newRegionSizes);
			tail = head;
		}

		new(tail->data+pushOff) T(std::move(mov));
		pushOff++;

		if (pushOff == tail->size) //add new region if last region is full
		{
			tail->next = new _PipeQRegion<T>(newRegionSizes);
			tail = tail->next;
			pushOff = 0;
		}

	}
	bool try_pop(T* out) //if out is null_ptr, then first object is destructed instead of moved
	{
		if (head == nullptr) //no data allocated
			return false;
		if (head == tail && popOff >= pushOff) //no data in head region
			return false;

		if (out != nullptr)
			*out = std::move(head->data[popOff]);
		else
			head->data[popOff].~T();
		popOff++;

		if (popOff == head->size)
		{
			_PipeQRegion<T>* next = head->next;
			delete head;
			head = next;
			popOff = 0;
		}

		return true;
	}
	bool try_pop(T& out)
	{
		return try_pop(&out);
	}
	T pop_front() 
	{
		if (head == nullptr) //no data allocated
			throw std::runtime_error("Attempted to pop from empty pipe queue");
		if (head == tail && popOff >= pushOff) //no data in head region
			throw std::runtime_error("Attempted to pop from empty pipe queue");

		T temp = std::move(head->data[popOff]);
		popOff++;

		if (popOff = head->size)
		{
			_PipeQRegion<T>* next = head->next;
			delete head;
			head = next;
			popOff = 0;
		}

		return std::move(temp);
	}

	~PipeQ()
	{
		while (try_pop(nullptr)) {}
		if (head != nullptr)
			delete head;
		head = nullptr;
		tail = nullptr;
	}

};