#pragma once

template<std::size_t N>
struct to_tuple_t;

template<>
struct to_tuple_t<3>
{
	template<class S>
	auto operator()( S&& s ) const
	{
		auto [e0, e1, e2] = std::forward<S>( s );
		return std::make_tuple( e0, e1, e2 );
	}
};

template<std::size_t N, class S>
auto to_tuple( S&& s )
{
	return to_tuple_t<N>{}( std::forward<S>( s ) );
}