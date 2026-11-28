struct Rectangle
    x = 0.0
    y = 0.0
    width = 0.0
    height = 0.0

    init
    end

    init x:float y:float width:float height:float
        this.x = x
        this.y = y
        this.width = width
        this.height = height
    end

    fun move deltaX:float deltaY:float
        this.x = this.x + deltaX
        this.y = this.y + deltaY
    end

    fun left
        return this.x
    end

    fun top
        return this.y
    end

    fun right
        return this.x + this.width
    end

    fun bottom
        return this.y + this.height
    end
end
