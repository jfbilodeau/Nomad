# Logging configuration types.

open hub log
    enum Level
        Debug
        Info
        Warning
        Error
        Critical
    end

    enum FileRotationMode
        None
        Size
    end

    enum DeliveryMode
        Buffered
        Synchronous
    end

    class SinkConfiguration
        active = true
        level = log.Level.Info
    end

    class FileSinkConfiguration
        active = true
        level = log.Level.Info
        directory = "logs"
        name = "application"
        rotationMode = log.FileRotationMode.Size
        rotateAtStartup = true
        maxSizeBytes = 10485760
        maxFiles = 10
    end
end
