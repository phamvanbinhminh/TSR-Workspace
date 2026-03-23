#pragma once

// Obstacle – AABB rectangle, toạ độ local trong region
// (tính từ góc trên-trái của region, không phải gốc map)
struct Obstacle
{
    float x = 0.f;   // local x
    float y = 0.f;   // local y
    float w = 32.f;  // width
    float h = 32.f;  // height
};