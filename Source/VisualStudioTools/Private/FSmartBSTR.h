#pragma once

#include "VisualStudioDTE.h"
#include <utility>

class FSmartBSTR
{
public:
	FSmartBSTR() : data(nullptr)
	{
	}

	FSmartBSTR(const FSmartBSTR& Other)
	{
		if (Other.data) data = SysAllocString(Other.data);
		else data = nullptr;
	}

	FSmartBSTR(FSmartBSTR&& Other)
	{
		data = std::exchange(Other.data, nullptr);
	}

	FSmartBSTR(const FString& Other)
	{
		data = SysAllocString(*Other);
	}
	
	FSmartBSTR(const OLECHAR *Ptr)
	{
		if (Ptr) data = SysAllocString(Ptr);
		else data = nullptr;
	}
	
	~FSmartBSTR()
	{
		if (data) SysFreeString(data);
	}

	FSmartBSTR& operator=(const FSmartBSTR& Other)
	{
		if (this == &Other) return *this;
		if (data) SysFreeString(data);
		if (Other.data) data = SysAllocString(Other.data);
		else data = nullptr;
		return *this;
	}

	FSmartBSTR& operator=(FSmartBSTR&& Other)
	{
		if (data) SysFreeString(data);
		data = std::exchange(Other.data, nullptr);
		return *this;
	}
	
	BSTR operator*() const
	{
		return data;
	}

private:
	BSTR data;
};