local SPEED = 200

local moving = false
local lastDir = "down"

function Init()
    Entity.playAnimation("idle_down")
end

function Update(dt)
    local vx, vy = 0, 0
    moving = false

    local dir = lastDir

    -- Horizontal
    if Input.isKeyDown(Input.KEY_A) or Input.isKeyDown(Input.KEY_LEFT) then
        vx = -SPEED
        dir = "left"
        moving = true
    elseif Input.isKeyDown(Input.KEY_D) or Input.isKeyDown(Input.KEY_RIGHT) then
        vx = SPEED
        dir = "right"
        moving = true
    end

    -- Vertical (ưu tiên nếu có)
    if Input.isKeyDown(Input.KEY_W) or Input.isKeyDown(Input.KEY_UP) then
        vy = -SPEED
        dir = "up"
        moving = true
    elseif Input.isKeyDown(Input.KEY_S) or Input.isKeyDown(Input.KEY_DOWN) then
        vy = SPEED
        dir = "down"
        moving = true
    end

    Entity.setVelocity(vx, vy)

    if moving then
        lastDir = dir
        Entity.playAnimation("walk_" .. dir)
    else
        Entity.playAnimation("idle_" .. lastDir)
    end
end