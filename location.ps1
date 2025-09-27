# PowerShell script to toggle location services and get coordinates
# Requires Administrator privileges for location service changes

# Check if running as Administrator
if (-NOT ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Warning "This script requires Administrator privileges to modify location settings."
    Write-Host "Please run PowerShell as Administrator and try again."
    exit 1
}

# Function to check current location service status
function Get-LocationServiceStatus {
    try {
        $locationKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\location"
        $currentValue = Get-ItemProperty -Path $locationKey -Name "Value" -ErrorAction SilentlyContinue
        
        if ($currentValue.Value -eq "Allow") {
            return $true
        } else {
            return $false
        }
    }
    catch {
        Write-Warning "Could not read location service status from registry"
        return $null
    }
}

# Function to toggle location services
function Set-LocationService {
    param([bool]$Enable)
    
    try {
        $locationKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\location"
        
        if ($Enable) {
            Set-ItemProperty -Path $locationKey -Name "Value" -Value "Allow"
            Write-Host "Location services enabled" -ForegroundColor Green
        } else {
            Set-ItemProperty -Path $locationKey -Name "Value" -Value "Deny"
            Write-Host "Location services disabled" -ForegroundColor Yellow
        }
        
        # Also set per-user location access
        $userLocationKey = "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\location"
        if (Test-Path $userLocationKey) {
            if ($Enable) {
                Set-ItemProperty -Path $userLocationKey -Name "Value" -Value "Allow"
            } else {
                Set-ItemProperty -Path $userLocationKey -Name "Value" -Value "Deny"
            }
        }
        
        return $true
    }
    catch {
        Write-Error "Failed to modify location service settings: $_"
        return $false
    }
}

# Function to get current location coordinates
function Get-CurrentLocation {
    try {
        Write-Host "Attempting to get current location..." -ForegroundColor Cyan
        
        # Method 1: Using Windows Location API via COM
        try {
            Add-Type -AssemblyName System.Device
            $geoWatcher = New-Object System.Device.Location.GeoCoordinateWatcher
            $geoWatcher.Start()
            
            # Wait for location data (up to 10 seconds)
            $timeout = 0
            while ($geoWatcher.Position.Location.IsUnknown -and $timeout -lt 20) {
                Start-Sleep -Milliseconds 500
                $timeout++
            }
            
            if (-not $geoWatcher.Position.Location.IsUnknown) {
                $lat = $geoWatcher.Position.Location.Latitude
                $lon = $geoWatcher.Position.Location.Longitude
                $accuracy = $geoWatcher.Position.Location.HorizontalAccuracy
                
                $geoWatcher.Stop()
                $geoWatcher.Dispose()
                
                Write-Host "Location found using Windows Location API:" -ForegroundColor Green
                Write-Host "Latitude: $lat" -ForegroundColor White
                Write-Host "Longitude: $lon" -ForegroundColor White
                Write-Host "Accuracy: $accuracy meters" -ForegroundColor Gray
                return @{ Latitude = $lat; Longitude = $lon; Accuracy = $accuracy; Method = "Windows API" }
            }
            
            $geoWatcher.Stop()
            $geoWatcher.Dispose()
        }
        catch {
            Write-Warning "Windows Location API failed: $_"
        }
        
        # Method 2: Using IP-based geolocation as fallback
        try {
            Write-Host "Falling back to IP-based location..." -ForegroundColor Yellow
            $response = Invoke-RestMethod -Uri "http://ip-api.com/json/" -TimeoutSec 10
            
            if ($response.status -eq "success") {
                Write-Host "Location found using IP geolocation:" -ForegroundColor Green
                Write-Host "Latitude: $($response.lat)" -ForegroundColor White
                Write-Host "Longitude: $($response.lon)" -ForegroundColor White
                Write-Host "City: $($response.city), $($response.regionName), $($response.country)" -ForegroundColor Gray
                Write-Host "ISP: $($response.isp)" -ForegroundColor Gray
                
                return @{ 
                    Latitude = $response.lat; 
                    Longitude = $response.lon; 
                    City = $response.city;
                    Region = $response.regionName;
                    Country = $response.country;
                    ISP = $response.isp;
                    Method = "IP Geolocation" 
                }
            }
        }
        catch {
            Write-Warning "IP-based geolocation failed: $_"
        }
        
        Write-Error "Unable to determine location using any available method"
        return $null
    }
    catch {
        Write-Error "Error getting location: $_"
        return $null
    }
}

# Main execution
Write-Host "=== Location Service Manager ===" -ForegroundColor Magenta
Write-Host

# Check current location service status
Write-Host "Checking current location service status..." -ForegroundColor Cyan
$currentStatus = Get-LocationServiceStatus

if ($currentStatus -eq $null) {
    Write-Warning "Could not determine location service status"
} elseif ($currentStatus) {
    Write-Host "Location services are currently ENABLED" -ForegroundColor Green
    
    # Ask user if they want to turn it off
    $response = Read-Host "Location services are ON. Do you want to turn them OFF? (y/N)"
    if ($response -eq 'y' -or $response -eq 'Y') {
        if (Set-LocationService -Enable $false) {
            Write-Host "Location services have been turned OFF" -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "Location services are currently DISABLED" -ForegroundColor Red
    
    # Turn on location services as requested
    Write-Host "Turning ON location services..." -ForegroundColor Cyan
    if (Set-LocationService -Enable $true) {
        Write-Host "Location services have been turned ON" -ForegroundColor Green
        Start-Sleep -Seconds 2  # Give services time to start
    } else {
        Write-Error "Failed to enable location services"
        exit 1
    }
}

Write-Host

# Get current location
Write-Host "Getting current location coordinates..." -ForegroundColor Cyan
$location = Get-CurrentLocation

if ($location) {
    Write-Host
    Write-Host "=== LOCATION RESULTS ===" -ForegroundColor Magenta
    Write-Host "Method: $($location.Method)" -ForegroundColor Cyan
    Write-Host "Latitude: $($location.Latitude)" -ForegroundColor White
    Write-Host "Longitude: $($location.Longitude)" -ForegroundColor White
    
    if ($location.Accuracy) {
        Write-Host "Accuracy: $($location.Accuracy) meters" -ForegroundColor Gray
    }
    
    if ($location.City) {
        Write-Host "Location: $($location.City), $($location.Region), $($location.Country)" -ForegroundColor Gray
    }
    
    # Generate Google Maps link
    $mapsUrl = "https://www.google.com/maps?q=$($location.Latitude),$($location.Longitude)"
    Write-Host "Google Maps: $mapsUrl" -ForegroundColor Blue
} else {
    Write-Host "Could not retrieve location coordinates" -ForegroundColor Red
}

Write-Host
Write-Host "Script completed." -ForegroundColor Green
