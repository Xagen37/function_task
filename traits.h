#pragma once
#include <exception>
#include <type_traits>


constexpr static size_t INPLACE_BUFFER_SIZE = sizeof(void*);
constexpr static size_t INPLACE_BUFFER_ALIGNMENT = alignof(void*);

using inplace_buffer = std::aligned_storage<
	INPLACE_BUFFER_SIZE,
	INPLACE_BUFFER_ALIGNMENT>::type;

template <typename T>
constexpr static bool fits_small_storage = 
	sizeof(T) <= INPLACE_BUFFER_SIZE &&
	INPLACE_BUFFER_ALIGNMENT % alignof(T) == 0 &&
	std::is_nothrow_move_constructible<T>::value;

struct bad_function_call : std::exception
{
	const char* what() const noexcept override
	{
		return "bad function call";
	}
};

namespace function_details
{
	template <typename Sign>
	struct storage;

	template <typename Return_t, typename... Args>
	struct type_descriptor
	{
		using storage_t = storage<Return_t(Args...)>;

		void (*copy)(const storage_t* src, storage_t* dest);
		void (*move)(storage_t* src, storage_t* dest);
		Return_t(*invoke)(const storage_t* src, Args...);
		void (*destroy)(storage_t*);
	};

	template <typename Return_t, typename... Args>
	type_descriptor <Return_t, Args...> const* empty_type_descriptor()
	{
		using storage_t = storage <Return_t(Args...)>;

		constexpr static type_descriptor <Return_t, Args...> impl =
		{
			[](storage_t const* src, storage_t* dest)
			{
				dest->set_desc(src->get_desc());
			},
			[](storage_t* src, storage_t* dest)
			{
				dest->set_desc(src->get_desc());
			},
			[](const storage_t* src, Args...) -> Return_t
			{
				throw bad_function_call();
			},
			[](storage_t*) {}
		};

		return &impl;
	}

	template <typename T, typename = void>
	struct function_traits;

	template <typename Return_t, typename... Args>
	struct storage <Return_t(Args...)>
	{
		storage()
			: descriptor(empty_type_descriptor<Return_t, Args...>())
		{}

		template<typename T>
		storage(T&& val)
		{
			if constexpr (fits_small_storage<T>)
			{
				new(&buffer) std::remove_reference_t<T>(std::forward<T>(val));
			}
			else
			{
				function_traits<T>::initialize_storage(this, std::move(val));
			}
			set_desc(function_traits<T>::template get_type_descriptor<Return_t, Args...>());
		}

		~storage()
		{
			descriptor->destroy(this);
		}

		explicit operator bool() const noexcept
		{
			return descriptor != empty_type_descriptor <Return_t, Args...>();
		}

		template <typename T>
		T& get_static() noexcept
		{
			return reinterpret_cast<T&>(buffer);
		}

		template <typename T>
		const T& get_static() const noexcept
		{
			return reinterpret_cast<const T&>(buffer);
		}

		void set_dynamic(void* value) noexcept
		{
			reinterpret_cast<void*&>(buffer) = value;
		}

		template <typename T>
		T* get_dynamic() const noexcept
		{
			return reinterpret_cast<T* const&>(buffer);
		}

		template <typename T>
		T const* target() const noexcept
		{
			if (function_traits<T>::template get_type_descriptor<Return_t, Args...>() == descriptor)
			{
				if constexpr (fits_small_storage<T>)
				{
					return &get_static<T>();
				}
				else
				{
					return get_dynamic<T>();
				}
			}
			else
			{
				return nullptr;
			}
		}

		Return_t invoke(Args&& ... args) const
		{
			return descriptor->invoke(this, std::forward<Args>(args)...);
		}

		const inplace_buffer& get_buffer() const
		{
			return buffer;
		}

		inplace_buffer& get_buffer()
		{
			return buffer;
		}

		const type_descriptor<Return_t, Args...>* get_desc() const
		{
			return descriptor;
		}

		void set_desc(const type_descriptor<Return_t, Args...>* other_desc)
		{
			descriptor = other_desc;
		}

	private:
		inplace_buffer buffer;
		const type_descriptor<Return_t, Args...>* descriptor;
	};

	template <typename T>
	struct function_traits <T, std::enable_if_t<fits_small_storage<T>>>
	{
		template <typename Return_t, typename... Args>
		static const type_descriptor <Return_t, Args...>* get_type_descriptor() noexcept
		{
			using storage_t = storage <Return_t(Args...)>;

			constexpr static type_descriptor <Return_t, Args...> impl =
			{
				// Copy
				[](const storage_t* src, storage_t* dest)
				{
					new (&(dest->get_buffer())) T(src->template get_static<T>());
					dest->set_desc(src->get_desc());
				},

				// move
				[](storage_t* src, storage_t* dest)
				{
					new (&(dest->get_buffer())) T(std::move(src->template get_static<T>()));
					dest->set_desc(src->get_desc());
					src->set_desc(empty_type_descriptor<Return_t, Args...>());
				},

				// invoke
				[](const storage_t* src, Args... args) -> Return_t
				{
					return src->template get_static<T>()(std::forward<Args>(args)...);
				},

				// destroy
				[](storage_t* src)
				{
					src->template get_static<T>().~T();
					src->set_desc(empty_type_descriptor<Return_t, Args...>());
				}
			};

			return &impl;
		}
	};

	template <typename T>
	struct function_traits<T, std::enable_if_t<!fits_small_storage<T>>>
	{
		template <typename Return_t, typename... Args>
		static void initialize_storage(storage <Return_t(Args...)>* src, T&& obj)
		{
			src->set_dynamic(new T(std::move(obj)));
		}

		template <typename Return_t, typename... Args>
		static void set_storage(storage <Return_t(Args...)>* src, T* val)
		{
			src->set_dynamic(val);
		}

		template <typename Return_t, typename... Args>
		static const type_descriptor <Return_t, Args...>* get_type_descriptor() noexcept
		{
			using storage_t = storage <Return_t(Args...)>;

			constexpr static type_descriptor <Return_t, Args...> impl =
			{
				// Copy
				[](const storage_t* src, storage_t* dest)
				{
					set_storage(dest, new T(*src->template get_dynamic<T>()));
					dest->set_desc(src->get_desc());
				},

				// move
				[](storage_t* src, storage_t* dest)
				{
					set_storage(dest, src->template get_dynamic<T>());
					dest->set_desc(src->get_desc());
					src->set_desc(empty_type_descriptor<Return_t, Args...>());
				},

				// invoke
				[](const storage_t* src, Args... args)
				{
					return (*src->template get_dynamic<T>())(std::forward<Args>(args)...);
				},

				// destroy
				[](storage_t* src)
				{
					delete src->template get_dynamic<T>();
					src->set_desc(empty_type_descriptor<Return_t, Args...>());
				}
			};

			return &impl;
		}
	};
} // function_details namespace