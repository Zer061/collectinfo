Liet ke toan bo profiles Chrome:

Get-ChildItem "$env:LOCALAPPDATA\Google\Chrome\User Data" -Directory | Where-Object { $_.Name -match '^Profile|^Default$' } | ForEach-Object { $_.FullName }
