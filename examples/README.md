# MCUForge 示例输入与输出

这些文件用于复现演示和帮助新人理解正确交互，不是固定测试答案。

推荐顺序：

1. 在 `Manager: default` 发送 `requests/create-project.txt`；
2. 进入自动创建的项目房间；
3. 发送 `requests/change-request.txt`；
4. 核对 Lead 的回复是否具备 `expected/intake-draft.md` 中的结构；
5. 修改任意一条，观察草案更新；
6. 明确回复“可以了，开始执行”；
7. 核对后续进度是否符合 `expected/progress.md`。

示例不包含 API Key、账号密码、proposal approval token 或真实 COM 口。不要把个人凭据补进这些公开文件。
