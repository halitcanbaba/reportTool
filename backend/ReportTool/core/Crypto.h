//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                  Crypto.h - AES-256-GCM via Windows BCrypt       |
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Crypto
{
   //--- Initialise once with the master key (32 bytes). Source: env REPORTTOOL_MASTER_KEY (64 hex chars).
   bool Init(const std::vector<uint8_t>& key32, std::string* err = nullptr);
   void Shutdown();

   //--- 64-hex -> 32 bytes. Returns empty on failure.
   std::vector<uint8_t> HexToKey(const std::string& hex);

   //--- AES-256-GCM. Output blob = base64(iv|ciphertext|tag). 12-byte IV, 16-byte tag.
   std::string EncryptB64(const std::string& plaintext);
   bool        DecryptB64(const std::string& blob_b64, std::string* out_plain);

   //--- Raw blob variants if you don't want base64 framing.
   bool Encrypt(const uint8_t* plain, size_t plain_len, std::vector<uint8_t>* out_blob);
   bool Decrypt(const uint8_t* blob,  size_t blob_len,  std::string* out_plain);

   std::string Base64Encode(const uint8_t* data, size_t len);
   std::vector<uint8_t> Base64Decode(const std::string& s);

   //--- URL-safe base64 (no padding): used for session token cookies.
   std::string Base64UrlEncode(const uint8_t* data, size_t len);

   //--- Cryptographically secure random bytes (BCryptGenRandom).
   std::vector<uint8_t> RandomBytes(size_t n);

   //--- Password hashing (PBKDF2-HMAC-SHA256, 200k iterations).
   //--- Stored format: "pbkdf2$<iter>$<salt_b64>$<hash_b64>".
   std::string HashPassword(const std::string& plain);
   bool        VerifyPassword(const std::string& plain, const std::string& stored);
}
