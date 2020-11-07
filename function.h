#pragma once
#include "traits.h"


template <typename F>
struct function;

template <typename Return_t, typename... Args>
struct function<Return_t (Args...)>
{
	using storage_t = function_details::storage<Return_t(Args...)>;

	function() = default;

	function(const function& other)
	{
		other.stg.get_desc()->copy(&other.stg, &stg);
	}

	function(function&& other) noexcept
	{
		other.stg.get_desc()->move(&other.stg, &stg);
	}
	
    template <typename T>
	function(T val)
		: stg(std::move(val))
	{}

	function& operator=(function const& rhs)
	{
		if (this != &rhs)
		{
			storage_t temp_copy;
			rhs.stg.get_desc()->copy(&rhs.stg, &temp_copy);

			stg.get_desc()->destroy(&stg);
			temp_copy.get_desc()->move(&temp_copy, &stg);
		}
		
		return *this;
	}
	function& operator=(function&& rhs) noexcept
	{
		if (this != &rhs)
		{
			stg.get_desc()->destroy(&stg);
			rhs.stg.get_desc()->move(&rhs.stg, &stg);
		}
		
		return *this;
	}

	explicit operator bool() const noexcept
	{
		return static_cast<bool>(stg);
	}

	Return_t operator()(Args... args) const
	{
		return stg.invoke(std::forward<Args>(args)...);
	}

    template <typename T>
	T* target() noexcept
	{
		if (*this)
		{
			return const_cast<T*>(static_cast<const function<Return_t(Args...)> *>(this)->template target<T>());
		}
		else
		{
			return nullptr;
		}
	}

    template <typename T>
	T const* target() const noexcept
	{
		if (*this)
		{
			return stg.template target<T>();
		}
		else
		{
			return nullptr;
		}
	}

private:
	storage_t stg;
};
