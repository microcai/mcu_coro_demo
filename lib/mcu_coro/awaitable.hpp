
#pragma once

#include <coroutine>
#include "mcu_coro.hpp"

namespace mcucoro
{
	template<typename T>
	struct await_transformer;

	template<typename T>
	struct awaitable;

	template<typename T>
	struct awaitable_promise;

	template<typename T, typename CallbackFunction>
	struct CallbackAwaiter;

	template<typename T>
	struct local_storage_t
	{
	};

	inline constexpr local_storage_t<void> local_storage;

	//////////////////////////////////////////////////////////////////////////
	namespace concepts
	{
		// 类型是否是 local_storage_t<> 的一种
		template<typename T>
		concept local_storage_type = requires(T x)
		{
			{ local_storage_t{x} } -> std::same_as<T>;
		};

		// 类型是否是 awaitable<> 的一种
		template<typename T>
		concept awaitable_type = requires(T x)
		{
			{ awaitable{x} } -> std::same_as<T>;
		};

		// 类型是否是 awaitable_promise<> 的一种
		template<typename T>
		concept awaitable_promise_type = requires(T x)
		{
			{ awaitable_promise{x} } -> std::same_as<T>;
		};

		// await_suspend 有三种返回值
		template<typename T>
		concept is_valid_await_suspend_return_value = std::convertible_to<T, std::coroutine_handle<>> ||
													std::is_void_v<T> ||
													std::is_same_v<T, bool>;

		// 用于判定 T 是否是一个 awaiter 的类型, 即: 拥有 await_ready，await_suspend，await_resume 成员函数的结构或类.
		template<typename T>
		concept is_awaiter_v = requires (T a)
		{
			{ a.await_ready() } -> std::same_as<bool>;
			{ a.await_suspend(std::coroutine_handle<>{}) } -> is_valid_await_suspend_return_value;
			{ a.await_resume() };
		};

		template<typename T>
		concept has_operator_co_await = requires (T a)
		{
			{ a.operator co_await() } -> is_awaiter_v;
 		};

		// 用于判定 T 是可以用在 co_await 后面
		template<typename T>
		concept is_awaitable_v = is_awaiter_v<typename std::decay_t<T>> ||
									awaitable_type<T> ||
		 							has_operator_co_await<typename std::decay_t<T>>;


		template<typename T>
		concept has_user_defined_await_transformer = requires (T&& a)
		{
			await_transformer<T>::await_transform(std::move(a));
		};

		template<typename T>
		struct is_not_awaitable : std::false_type{};

	} // namespace concepts

	namespace traits
	{
		//////////////////////////////////////////////////////////////////////////
		// 用于从 A = U<T> 类型里提取 T 参数
		// 比如
		// template_parameter_of<local_storage_t<int>, local_storage_t>;  // int
		// template_parameter_of<decltype(local_storage), local_storage_t>;  // void
		//
		// 首先定义一个接受 template_parameter_of<Testee, FromTemplate> 这样的一个默认模板萃取
		template<typename Testee, template<typename> typename FromTemplate>
		struct template_parameter_traits;

		// 接着定义一个偏特化，匹配 template_parameter_traits<模板名<参数>, 模板名>
		// 这样，这个偏特化的 template_parameter_traits 就有了一个
		// 名为 template_parameter 的成员类型，其定义的类型就是 _template_parameter
		// 于是就把 TemplateParameter 这个类型给萃取出来了
		template<template<typename> typename ClassTemplate, typename TemplateParameter>
		struct template_parameter_traits<ClassTemplate<TemplateParameter>, ClassTemplate>
		{
			using template_parameter = TemplateParameter ;
		};

		// 最后，定义一个简化用法的 using 让用户的地方代码变短点
		template<typename TesteeType, template<typename> typename FromTemplate>
		using template_parameter_of = typename template_parameter_traits<
									std::decay_t<TesteeType>, FromTemplate>::template_parameter;

		// 利用 通用工具 template_parameter_of 萃取 local_storage_t<T> 里的 T
		template<concepts::local_storage_type LocalStorage>
		using local_storage_value_type = template_parameter_of<LocalStorage, local_storage_t>;


		// 利用 通用工具 template_parameter_of 萃取 awaitable<T> 里的 T
		template<concepts::awaitable_type AwaitableType>
		using awaitable_return_type = template_parameter_of<AwaitableType, awaitable>;

	} // namespace traits

	struct debug_coro_promise
	{
#if defined(DEBUG_CORO_PROMISE_LEAK)

		void* operator new(std::size_t size)
		{
			void* ptr = std::malloc(size);
			if (!ptr)
			{
				throw std::bad_alloc{};
			}
			debug_coro_leak.insert(ptr);
			return ptr;
		}

		void operator delete(void* ptr, [[maybe_unused]] std::size_t size)
		{
			debug_coro_leak.erase(ptr);
			std::free(ptr);
		}

#endif // DEBUG_CORO_PROMISE_LEAK
	};

	//////////////////////////////////////////////////////////////////////////
	// 存储协程 promise 的返回值
	template<typename T>
	struct awaitable_promise_value
	{
		template<typename V>
		void return_value(V&& val) noexcept
		{
			value_ = val;
		}

		void unhandled_exception() noexcept
		{
		}

		T get_value() const
		{
            return value_;
		}

		T value_;
	};

	//////////////////////////////////////////////////////////////////////////
	// 存储协程 promise 的返回值 void 的特化实现
	template<>
	struct awaitable_promise_value<void>
	{
		constexpr void return_void() noexcept
		{
		}

		void unhandled_exception() noexcept
		{
		}

		void get_value() const
		{
		}
	};

	//////////////////////////////////////////////////////////////////////////

	template<typename T>
	struct final_awaitable
	{
		awaitable_promise<T> * parent;

		constexpr void await_resume() noexcept
		{
			// 并且，如果协程处于 .detach() 而没有被 co_await
			// 则异常一直存储在 promise 里，并没有代码会去调用他的 await_resume() 重抛异常
			// 所以这里重新抛出来，避免有被静默吞并的异常
			parent->get_value();
		}

		bool await_ready() noexcept
		{
			// continuation_ 不为空，则 说明 .detach() 被 co_await, 则
			// 返回 continuation_，以便让协程框架调用 continuation_.resume()
			// 这样就把等它的协程唤醒了.
			return !parent->continuation_;
			// 如果 continuation_ 为空，则说明此乃调用链上的最后一个 promise
			// 返回 true 让协程框架 自动调用 coroutine_handle::destory()
		}

		std::coroutine_handle<> await_suspend(std::coroutine_handle<awaitable_promise<T>> h) noexcept
		{
			return h.promise().continuation_;
		}
	};

	//////////////////////////////////////////////////////////////////////////
	// 返回 T 的协程 awaitable_promise 实现.

	// Promise 类型实现...
	template<typename T>
	struct awaitable_promise : public awaitable_promise_value<T>, public debug_coro_promise
	{
		awaitable<T> get_return_object();

		auto final_suspend() noexcept
		{
			return final_awaitable<T>{this};
		}

		auto initial_suspend()
		{
			return std::suspend_always{};
		}

		std::coroutine_handle<> continuation_;
	};

	//////////////////////////////////////////////////////////////////////////

	// awaitable 协程包装...
	template<typename T>
	struct awaitable
	{
		using promise_type = awaitable_promise<T>;
		std::coroutine_handle<promise_type> current_coro_handle_;

		explicit awaitable(std::coroutine_handle<promise_type> h)
			: current_coro_handle_(h)
		{
		}

		~awaitable()
		{
			if (current_coro_handle_)
			{
				if (current_coro_handle_.done())
				{
					current_coro_handle_.destroy();
				}
				else
				{
					current_coro_handle_.resume();
				}
			}
		}

		awaitable(awaitable&& t) noexcept
			: current_coro_handle_(t.current_coro_handle_)
		{
			t.current_coro_handle_ = nullptr;
		}

		awaitable& operator=(awaitable&& t) noexcept
		{
			if (&t != this)
			{
				if (current_coro_handle_)
				{
					current_coro_handle_.destroy();
				}
				current_coro_handle_ = t.current_coro_handle_;
				t.current_coro_handle_ = nullptr;
			}
			return *this;
		}

		awaitable(const awaitable&) = delete;
		awaitable(awaitable&) = delete;
		awaitable& operator=(const awaitable&) = delete;
		awaitable& operator=(awaitable&) = delete;

		constexpr bool await_ready() const noexcept
		{
			return false;
		}

		T await_resume()
		{
			return current_coro_handle_.promise().get_value();
		}

		template<typename PromiseType>
		auto await_suspend(std::coroutine_handle<PromiseType> continuation)
		{
			current_coro_handle_.promise().continuation_ = continuation;
			return current_coro_handle_;
		}

		auto detach()
		{
			auto launched_coro = [](awaitable<T> lazy) mutable -> awaitable<T>
			{
				co_return co_await std::move(lazy);
			}(std::move(*this));


			return launched_coro;
		}
    };

	//////////////////////////////////////////////////////////////////////////

	template<typename T>
	awaitable<T> awaitable_promise<T>::get_return_object()
	{
		auto result = awaitable<T>{std::coroutine_handle<awaitable_promise<T>>::from_promise(*this)};
		return result;
	}
}

template<typename T, typename CallbackFunction>
struct CallbackAwaiter
{
public:
    // using CallbackFunction = std::function<void(std::function<void(T)>)>;

    CallbackAwaiter(CallbackFunction callback_function)
        : callback_function_(std::move(callback_function)) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle)
    {
        callback_function_([handle = std::move(handle), this](T t) mutable
        {
            result_ = std::move(t);
            handle.resume();
        });
    }

    T await_resume() noexcept { return std::move(result_); }

private:
    CallbackFunction callback_function_;
    T result_;
};

template<typename CallbackFunction>
struct CallbackAwaiter<void, CallbackFunction>
{
public:
    // using CallbackFunction = std::function<void(std::function<void()>)>;
    CallbackAwaiter(CallbackFunction callback_function)
        : callback_function_(std::move(callback_function)) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        callback_function_(handle);
    }
    void await_resume() noexcept { }

private:
    CallbackFunction callback_function_;
};

template<typename T, typename callback>
CallbackAwaiter<T, callback>
awaitable_to_callback(callback cb)
{
    return CallbackAwaiter<T, callback>{cb};
}

template<typename INT>
mcucoro::awaitable<void> coro_delay_ms(INT ms)
{
    co_return co_await awaitable_to_callback<void>([ms](std::coroutine_handle<> handle)
    {
        mcucoro::delay_ms(ms, handle);
    });
}

inline void start_coro(mcucoro::awaitable<void>&& awaitable_coro)
{
    mcucoro::post(awaitable_coro.current_coro_handle_);
}
