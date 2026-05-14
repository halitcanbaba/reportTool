//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                          Crypto.cpp              |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Crypto.h"

#pragma comment(lib, "bcrypt.lib")

namespace
{
   BCRYPT_ALG_HANDLE  g_alg  = nullptr;
   BCRYPT_KEY_HANDLE  g_key  = nullptr;
   bool               g_init = false;
   constexpr DWORD    IV_LEN  = 12;
   constexpr DWORD    TAG_LEN = 16;
}

bool Crypto::Init(const std::vector<uint8_t>& key32, std::string* err)
{
   if(g_init) return true;
   if(key32.size() != 32) { if(err) *err = "Master key must be 32 bytes"; return false; }

   NTSTATUS s = BCryptOpenAlgorithmProvider(&g_alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
   if(s != 0) { if(err) *err = "BCryptOpenAlgorithmProvider failed"; return false; }

   s = BCryptSetProperty(g_alg, BCRYPT_CHAINING_MODE,
                         (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                         (ULONG)((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(WCHAR)), 0);
   if(s != 0) { if(err) *err = "Set GCM mode failed"; BCryptCloseAlgorithmProvider(g_alg, 0); g_alg = nullptr; return false; }

   s = BCryptGenerateSymmetricKey(g_alg, &g_key, nullptr, 0, (PUCHAR)key32.data(), (ULONG)key32.size(), 0);
   if(s != 0) { if(err) *err = "BCryptGenerateSymmetricKey failed"; BCryptCloseAlgorithmProvider(g_alg, 0); g_alg = nullptr; return false; }

   g_init = true;
   return true;
}

void Crypto::Shutdown()
{
   if(g_key) { BCryptDestroyKey(g_key); g_key = nullptr; }
   if(g_alg) { BCryptCloseAlgorithmProvider(g_alg, 0); g_alg = nullptr; }
   g_init = false;
}

std::vector<uint8_t> Crypto::HexToKey(const std::string& hex)
{
   if(hex.size() != 64) return {};
   std::vector<uint8_t> out(32);
   auto hexv = [](char c) -> int {
      if(c >= '0' && c <= '9') return c - '0';
      if(c >= 'a' && c <= 'f') return c - 'a' + 10;
      if(c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
   };
   for(size_t i = 0; i < 32; ++i)
   {
      int hi = hexv(hex[i*2]); int lo = hexv(hex[i*2+1]);
      if(hi < 0 || lo < 0) return {};
      out[i] = (uint8_t)((hi << 4) | lo);
   }
   return out;
}

bool Crypto::Encrypt(const uint8_t* plain, size_t plain_len, std::vector<uint8_t>* out_blob)
{
   if(!g_init) return false;

   std::vector<uint8_t> iv(IV_LEN);
   if(BCryptGenRandom(nullptr, iv.data(), IV_LEN, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;

   std::vector<uint8_t> tag(TAG_LEN);
   std::vector<uint8_t> ct(plain_len);

   BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
   BCRYPT_INIT_AUTH_MODE_INFO(info);
   info.pbNonce = iv.data();
   info.cbNonce = IV_LEN;
   info.pbTag   = tag.data();
   info.cbTag   = TAG_LEN;

   ULONG written = 0;
   NTSTATUS s = BCryptEncrypt(g_key, (PUCHAR)plain, (ULONG)plain_len, &info,
                               nullptr, 0,
                               ct.data(), (ULONG)ct.size(), &written, 0);
   if(s != 0) return false;
   ct.resize(written);

   out_blob->clear();
   out_blob->reserve(IV_LEN + ct.size() + TAG_LEN);
   out_blob->insert(out_blob->end(), iv.begin(), iv.end());
   out_blob->insert(out_blob->end(), ct.begin(), ct.end());
   out_blob->insert(out_blob->end(), tag.begin(), tag.end());
   return true;
}

bool Crypto::Decrypt(const uint8_t* blob, size_t blob_len, std::string* out_plain)
{
   if(!g_init) return false;
   if(blob_len < IV_LEN + TAG_LEN) return false;

   const uint8_t* iv = blob;
   const uint8_t* ct = blob + IV_LEN;
   size_t ct_len = blob_len - IV_LEN - TAG_LEN;
   const uint8_t* tag = blob + IV_LEN + ct_len;

   BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
   BCRYPT_INIT_AUTH_MODE_INFO(info);
   info.pbNonce = (PUCHAR)iv;
   info.cbNonce = IV_LEN;
   info.pbTag   = (PUCHAR)tag;
   info.cbTag   = TAG_LEN;

   std::vector<uint8_t> pt(ct_len);
   ULONG written = 0;
   NTSTATUS s = BCryptDecrypt(g_key, (PUCHAR)ct, (ULONG)ct_len, &info,
                               nullptr, 0,
                               pt.data(), (ULONG)pt.size(), &written, 0);
   if(s != 0) return false;
   out_plain->assign((const char*)pt.data(), written);
   return true;
}

std::string Crypto::EncryptB64(const std::string& plaintext)
{
   std::vector<uint8_t> blob;
   if(!Encrypt((const uint8_t*)plaintext.data(), plaintext.size(), &blob)) return "";
   return Base64Encode(blob.data(), blob.size());
}

bool Crypto::DecryptB64(const std::string& blob_b64, std::string* out_plain)
{
   auto blob = Base64Decode(blob_b64);
   if(blob.empty()) return false;
   return Decrypt(blob.data(), blob.size(), out_plain);
}

namespace
{
   const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string Crypto::Base64Encode(const uint8_t* data, size_t len)
{
   std::string out;
   out.reserve(((len + 2) / 3) * 4);
   for(size_t i = 0; i < len; i += 3)
   {
      uint32_t v = (uint32_t)data[i] << 16;
      if(i + 1 < len) v |= (uint32_t)data[i+1] << 8;
      if(i + 2 < len) v |= (uint32_t)data[i+2];
      out += B64[(v >> 18) & 0x3F];
      out += B64[(v >> 12) & 0x3F];
      out += (i + 1 < len) ? B64[(v >> 6) & 0x3F] : '=';
      out += (i + 2 < len) ? B64[v & 0x3F]        : '=';
   }
   return out;
}

std::vector<uint8_t> Crypto::Base64Decode(const std::string& s)
{
   auto v = [](char c) -> int {
      if(c >= 'A' && c <= 'Z') return c - 'A';
      if(c >= 'a' && c <= 'z') return c - 'a' + 26;
      if(c >= '0' && c <= '9') return c - '0' + 52;
      if(c == '+') return 62;
      if(c == '/') return 63;
      return -1;
   };
   std::vector<uint8_t> out;
   out.reserve(s.size() * 3 / 4);
   uint32_t buf = 0; int bits = 0;
   for(char c : s)
   {
      if(c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
      int x = v(c); if(x < 0) return {};
      buf = (buf << 6) | (uint32_t)x; bits += 6;
      if(bits >= 8) { bits -= 8; out.push_back((uint8_t)((buf >> bits) & 0xFF)); }
   }
   return out;
}

std::string Crypto::Base64UrlEncode(const uint8_t* data, size_t len)
{
   //--- Standard base64, then map +/ → -_ and strip padding (RFC 4648 §5).
   std::string s = Base64Encode(data, len);
   for(char& c : s) { if(c == '+') c = '-'; else if(c == '/') c = '_'; }
   while(!s.empty() && s.back() == '=') s.pop_back();
   return s;
}

std::vector<uint8_t> Crypto::RandomBytes(size_t n)
{
   std::vector<uint8_t> out(n);
   if(n == 0) return out;
   NTSTATUS s = BCryptGenRandom(nullptr, out.data(), (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
   if(s != 0) return {};
   return out;
}

namespace
{
   constexpr DWORD     kPbkdf2Iterations = 200000;
   constexpr size_t    kPbkdf2SaltLen    = 16;
   constexpr size_t    kPbkdf2HashLen    = 32;

   bool Pbkdf2Sha256(const std::string& plain, const uint8_t* salt, size_t salt_len,
                     DWORD iterations, size_t hash_len, std::vector<uint8_t>* out)
   {
      BCRYPT_ALG_HANDLE h = nullptr;
      NTSTATUS s = BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA256_ALGORITHM, nullptr,
                                                BCRYPT_ALG_HANDLE_HMAC_FLAG);
      if(s != 0) return false;
      out->assign(hash_len, 0);
      s = BCryptDeriveKeyPBKDF2(h,
                                 (PUCHAR)plain.data(), (ULONG)plain.size(),
                                 (PUCHAR)salt,        (ULONG)salt_len,
                                 (ULONGLONG)iterations,
                                 out->data(), (ULONG)hash_len, 0);
      BCryptCloseAlgorithmProvider(h, 0);
      return s == 0;
   }

   //--- Constant-time bytewise compare (avoids timing leaks on hash compare).
   bool ConstTimeEq(const uint8_t* a, const uint8_t* b, size_t n)
   {
      uint8_t v = 0;
      for(size_t i = 0; i < n; ++i) v |= (uint8_t)(a[i] ^ b[i]);
      return v == 0;
   }
}

std::string Crypto::HashPassword(const std::string& plain)
{
   auto salt = RandomBytes(kPbkdf2SaltLen);
   if(salt.empty()) return "";
   std::vector<uint8_t> hash;
   if(!Pbkdf2Sha256(plain, salt.data(), salt.size(), kPbkdf2Iterations, kPbkdf2HashLen, &hash))
      return "";
   char head[32];
   snprintf(head, sizeof(head), "pbkdf2$%u$", (unsigned)kPbkdf2Iterations);
   return std::string(head)
          + Base64Encode(salt.data(), salt.size()) + "$"
          + Base64Encode(hash.data(), hash.size());
}

bool Crypto::VerifyPassword(const std::string& plain, const std::string& stored)
{
   //--- Parse "pbkdf2$<iter>$<salt_b64>$<hash_b64>"
   if(stored.compare(0, 7, "pbkdf2$") != 0) return false;
   const size_t p1 = stored.find('$', 7);                if(p1 == std::string::npos) return false;
   const size_t p2 = stored.find('$', p1 + 1);           if(p2 == std::string::npos) return false;
   const DWORD iter = (DWORD)std::strtoul(stored.c_str() + 7, nullptr, 10);
   if(iter == 0 || iter > 10000000u) return false;
   const auto salt    = Base64Decode(stored.substr(p1 + 1, p2 - p1 - 1));
   const auto expect  = Base64Decode(stored.substr(p2 + 1));
   if(salt.empty() || expect.empty()) return false;
   std::vector<uint8_t> got;
   if(!Pbkdf2Sha256(plain, salt.data(), salt.size(), iter, expect.size(), &got)) return false;
   return ConstTimeEq(got.data(), expect.data(), expect.size());
}
