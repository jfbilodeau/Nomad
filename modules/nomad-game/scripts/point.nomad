struct Point
    x = 0.0
    y = 0.0

    init
    end

    init x:float y:float
        this.x = x
        this.y = y
    end

    fun move deltaX:float deltaY:float
        this.x = this.x + deltaX
        this.y = this.y + deltaY
    end
end
