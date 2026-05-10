# 删除旧 .git 并推送到新仓库
Set-Location "F:\chenyu_project\AI_YunCunChu"

# 1. 删除旧 .git
Write-Host "删除 .git..." -ForegroundColor Yellow
Remove-Item -Recurse -Force ".git"

# 2. 初始化新仓库
Write-Host "初始化新仓库..." -ForegroundColor Yellow
git init

# 3. 添加远程仓库
git remote add origin https://github.com/yu20120707/HydraFS.git

# 4. 添加所有文件并提交
Write-Host "添加文件..." -ForegroundColor Yellow
git add -A
git commit -m "Initial commit: AI_YunCunChu -> HydraFS"

# 5. 推送到远程
Write-Host "推送到远程..." -ForegroundColor Yellow
git branch -M main
git push -u origin main

Write-Host "完成!" -ForegroundColor Green
