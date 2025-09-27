Liet ke toan bo profiles Chrome:

Get-ChildItem "$env:LOCALAPPDATA\Google\Chrome\User Data" -Directory | Where-Object { $_.Name -match '^Profile|^Default$' } | ForEach-Object { $_.FullName }

Lay key giai ma --> sửa lại file và chạy decryptor

Lay thong tin location:
location.ps1


