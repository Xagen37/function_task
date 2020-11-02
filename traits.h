#include <type_traits>
#include <cstddef>
#include <utility>
#include <exception>

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

template <typename Sign>
struct type_descriptor;

template <typename T, typename Sign, typename = void>
struct function_traits;

template <typename Sign>
struct storage;

template <typename Return_t, typename... Args>
struct storage<Return_t(Args...)>
{
	storage()
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
			reinterpret_cast<T*&>(buffer) = new std::remove_reference_t<T>(std::forward<T>(val));
		}
		set_desc(function_traits<T, Return_t (Args...)>::get_type_descriptor());
	}

	~storage()
	{
		descriptor->destroy(this);
	}

	template <typename T>
	T& get_static() noexcept
	{
		if constexpr (fits_small_storage<T>)
		{
			return reinterpret_cast<T&>(buffer);
		}
		else
		{
			return *(reinterpret_cast<T* const &>(buffer));
		}
	}

	template <typename T>
	const T& get_static() const noexcept
	{
		if constexpr (fits_small_storage<T>)
		{
			return reinterpret_cast<const T&>(buffer);
		}
		else
		{
			return *(reinterpret_cast<const T* const &>(buffer));
		}
	}

	void set_dynamic(void* value) noexcept
	{
		reinterpret_cast<void*&>(buffer) = value;
	}

	template <typename T>
	T* get_dynamic() noexcept
	{
		return const_cast<T*>(static_cast <const storage*>(this)->template get_dynamic<T>());
	}

	template <typename T>
	const T* get_dynamic() const noexcept
	{
		if (function_traits<T, Return_t (Args...)>::get_type_descriptor() == descriptor)
		{
			return &(get_static<T>());
		}
		else
		{
			return nullptr;
		}
	}

	Return_t invoke(Args&&... args)
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

	const type_descriptor<Return_t (Args...)>* get_desc() const
	{
		return descriptor;
	}

	void set_desc(const type_descriptor<Return_t(Args...)>* other_desc)
	{
		descriptor = other_desc;
	}

private:
	inplace_buffer buffer;
	const type_descriptor<Return_t (Args...)>* descriptor;
};

template <typename Return_t, typename... Args>
struct type_descriptor <Return_t(Args...)>
{
	using storage_t = storage<Return_t (Args...)>;

	void (*copy)(const storage_t* src, storage_t* dest);
	void (*move)(storage_t* src, storage_t* dest);
	Return_t (*invoke)(storage_t* src, Args...);
	void (*destroy)(storage_t*);
};

template <typename T, typename Return_t, typename... Args>
struct function_traits <T, Return_t(Args...), std::enable_if_t<fits_small_storage<T>>>
{
	static const type_descriptor <Return_t (Args...)>* get_type_descriptor() noexcept
	{
		using storage_t = storage <Return_t (Args...)>;

		constexpr static type_descriptor <Return_t (Args...)> impl =
		{
			// Copy
			[](const storage_t* src, storage_t* dest) 
			{
				auto& f = src->template get_static<T>();
				new(&(dest->get_buffer())) T(f);
				dest->set_desc(src->get_desc());
			},

			// move
			[](storage_t* src, storage_t* dest)
			{
				new(&(dest->get_buffer())) T(std::move(src->template get_static<T>()));
				dest->set_desc(src->get_desc());
			},

			// invoke
			[](storage_t* src, Args... args) -> Return_t
			{
				return src->template get_static<T>()(std::forward<Args>(args)...);
			},
			
			// destroy
			[](storage_t* src)
			{
				src->template get_static<T>().~T();
			}
		};

		return &impl;
	}
};

template <typename T, typename Return_t, typename... Args>
struct function_traits<T, Return_t(Args...), std::enable_if_t<!fits_small_storage<T>>>
{
	static void initialize_storage(storage <Return_t (Args...)>& src, T&& obj)
	{
		src.set_dynamic(new T(std::move(obj)));
	}

	static void initialize_storage(storage <Return_t(Args...)>& src, const T& obj)
	{
		src.set_dynamic(new T(obj));
	}

	static const type_descriptor <Return_t (Args...)>* get_type_descriptor() noexcept
	{
		using storage_t = storage <Return_t (Args...)>;

		constexpr static type_descriptor <Return_t (Args...)> impl =
		{
			// Copy
			[](const storage_t* src, storage_t* dest) 
			{
				initialize_storage(*dest, src->template get_static<T>());
				dest->set_desc(src->get_desc());
			},

			// move
			[](storage_t* src, storage_t* dest) 
			{
				initialize_storage(*dest, src->template get_static<T>());
				dest->set_desc(src->get_desc());
			},

			// invoke
			[](storage_t* src, Args... args) 
			{
				return src->template get_static<T>()(std::forward<Args>(args)...);
			},

			// destroy
			[](storage_t* src) 
			{
				delete src->template get_dynamic<T>();
			}
		};

		return &impl;
	}
};