#pragma once
#include "session.hpp"

namespace metric { namespace evolution {
constexpr Material home{
    {{{0,0,true},{2,1,false},{4,3,false},{7,5,true},{9,3,false},{12,0,true}}},
    {{{0,0,true},{2,2,false},{4,3,false},{6,4,true},{8,2,false},{11,0,true}}},
    3, Role::departure
};
constexpr Material destination{
    {{{0,0,true},{3,2,false},{5,4,false},{8,5,true},{11,4,false},{14,2,true}}},
    {{{0,2,true},{1,3,false},{3,5,false},{5,6,true},{9,4,false},{13,2,true}}},
    2, Role::resolution
};
}} // namespace metric::evolution
