#pragma once
#include "traits.h"
#include <memory>


template <typename F>
struct function;

template <typename Return_t, typename... Args>
struct function<Return_t (Args...)>
{
	using storage_t = storage<Return_t(Args...)>;

	function() = default;

	function(const function& other)
	{
		if (!other)
		{
			stg = std::unique_ptr<storage_t>(nullptr);
		}
		else
		{
			if (!stg.get())
			{
				stg = std::unique_ptr<storage_t>(new storage_t());
			}
			other.stg->get_desc()->copy(other.stg.get(), stg.get());
		}
	}

	function(function&& other) noexcept
	{
		if (!other)
		{
			stg = std::unique_ptr<storage_t>(nullptr);
		}
		else
		{
			if (!stg.get())
			{
				stg = std::unique_ptr<storage_t>(new storage_t());
			}
			other.stg->get_desc()->move(other.stg.get(), stg.get());
		}
	}
	
    template <typename T>
	function(T val)
		: stg(std::make_unique <storage_t> (std::move(val)))
	{}

	function& operator=(function const& rhs)
	{
		if (this != &rhs)
		{
			if (!rhs)
			{
				stg.reset();
			}
			else
			{
				if (!stg.get())
				{
					stg = std::unique_ptr<storage_t>(new storage_t());
				}
				rhs.stg->get_desc()->copy(rhs.stg.get(), stg.get());
			}
		}
		
		return *this;
	}
	function& operator=(function&& rhs) noexcept
	{
		stg.swap(rhs.stg);

		return *this;
	}

	explicit operator bool() const noexcept
	{
		return stg.get();
	}

	Return_t operator()(Args... args) const
	{
		if (!stg.get())
		{
			throw bad_function_call();
		}
		else
		{
			return stg->invoke(std::forward<Args>(args)...);
		}
	}

    template <typename T>
	T* target() noexcept
	{
		if (*this)
		{
			return stg->template get_dynamic<T>();
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
			return stg->template get_dynamic<T>();
		}
		else
		{
			return nullptr;
		}
	}

private:
	std::unique_ptr<storage_t> stg;
};
