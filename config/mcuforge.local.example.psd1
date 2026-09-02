@{
    # 复制本文件为 mcuforge.local.psd1 后再改；该本地文件不要提交。
    HiClawEnvPath = 'C:\Users\YOUR_NAME\hiclaw-manager.env'
    Controller     = 'hiclaw-controller'

    # Team 默认使用 deepseek-v4-flash。若你的 HiClaw 配置使用其他模型，
    # 运行 Bootstrap-MCUForgeTeam.ps1 时通过 -Model 显式覆盖。
    Model          = 'deepseek-v4-flash'

    # 仅供文档和人工核对；Bridge 默认端口由现有启动脚本固定。
    Stm32BridgePort   = 8765
    ResearchBridgePort = 8766
    ElementWebUrl     = 'http://127.0.0.1:18088'
    ManagerConsoleUrl = 'http://127.0.0.1:18888'
}
