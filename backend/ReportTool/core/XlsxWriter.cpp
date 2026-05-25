//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       XlsxWriter.cpp             |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "XlsxWriter.h"
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace
{
   //--- CRC32 (IEEE 802.3) — needed by ZIP local file headers and central
   //--- directory entries. Table-driven for speed; computed once at first
   //--- use. Polynomial 0xEDB88320 (reflected).
   uint32_t Crc32(const std::string& data)
   {
      static uint32_t table[256];
      static bool     ready = false;
      if(!ready)
      {
         for(uint32_t i = 0; i < 256; ++i)
         {
            uint32_t c = i;
            for(int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & -(int32_t)(c & 1));
            table[i] = c;
         }
         ready = true;
      }
      uint32_t crc = 0xFFFFFFFFu;
      for(unsigned char b : data) crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
      return crc ^ 0xFFFFFFFFu;
   }

   //--- Little-endian writers — ZIP is strictly LE, all sizes / offsets /
   //--- signatures included.
   void Put16(std::string& out, uint16_t v) { out += (char)(v & 0xFF); out += (char)((v >> 8) & 0xFF); }
   void Put32(std::string& out, uint32_t v) {
      out += (char)(v & 0xFF);
      out += (char)((v >> 8)  & 0xFF);
      out += (char)((v >> 16) & 0xFF);
      out += (char)((v >> 24) & 0xFF);
   }

   //--- DOS date/time. We don't care about precision here; use a fixed
   //--- 2020-01-01 00:00 so the zip is reproducible (Telegram chat reads
   //--- same hash for same payload).
   constexpr uint16_t kDosTime = 0;                          // 00:00:00
   constexpr uint16_t kDosDate = ((2020 - 1980) << 9) | (1 << 5) | 1;  // 2020-01-01

   //--- ZIP entry being accumulated. We hold central directory entries
   //--- separately so they can be appended after every file payload, in
   //--- the order they were written, as the format demands.
   struct ZipEntry {
      std::string name;
      uint32_t    crc;
      uint32_t    size;       // == compressed size in store mode
      uint32_t    local_off;  // offset of this entry's local file header
   };

   //--- Append one stored (uncompressed) file. Returns the bookkeeping
   //--- needed when the central directory is later flushed.
   ZipEntry WriteZipEntry(std::string& zip, const std::string& name, const std::string& data)
   {
      ZipEntry e;
      e.name      = name;
      e.crc       = Crc32(data);
      e.size      = (uint32_t)data.size();
      e.local_off = (uint32_t)zip.size();

      //--- Local file header (PK\x03\x04 + 26 fixed bytes + filename).
      Put32(zip, 0x04034b50);     // signature
      Put16(zip, 20);             // version needed: 2.0 (store)
      Put16(zip, 0);              // flags
      Put16(zip, 0);              // method: 0 = stored
      Put16(zip, kDosTime);
      Put16(zip, kDosDate);
      Put32(zip, e.crc);
      Put32(zip, e.size);         // compressed size
      Put32(zip, e.size);         // uncompressed size
      Put16(zip, (uint16_t)name.size());
      Put16(zip, 0);              // extra field length
      zip += name;
      zip += data;
      return e;
   }

   //--- Append the central directory + end record. After this, `zip` is a
   //--- valid .zip stream consumable by Excel.
   void FinishZip(std::string& zip, const std::vector<ZipEntry>& entries)
   {
      const uint32_t cdir_off = (uint32_t)zip.size();
      for(const auto& e : entries)
      {
         Put32(zip, 0x02014b50); // central dir header signature
         Put16(zip, 0x031E);     // version made by (0x031E = 3.0, UNIX)
         Put16(zip, 20);         // version needed
         Put16(zip, 0);          // flags
         Put16(zip, 0);          // method
         Put16(zip, kDosTime);
         Put16(zip, kDosDate);
         Put32(zip, e.crc);
         Put32(zip, e.size);
         Put32(zip, e.size);
         Put16(zip, (uint16_t)e.name.size());
         Put16(zip, 0);          // extra
         Put16(zip, 0);          // comment
         Put16(zip, 0);          // disk #
         Put16(zip, 0);          // internal attrs
         Put32(zip, 0);          // external attrs
         Put32(zip, e.local_off);
         zip += e.name;
      }
      const uint32_t cdir_size = (uint32_t)zip.size() - cdir_off;

      //--- End-of-central-directory record (22 bytes).
      Put32(zip, 0x06054b50);
      Put16(zip, 0);                                // disk #
      Put16(zip, 0);                                // disk with cdir
      Put16(zip, (uint16_t)entries.size());         // entries on this disk
      Put16(zip, (uint16_t)entries.size());         // total entries
      Put32(zip, cdir_size);
      Put32(zip, cdir_off);
      Put16(zip, 0);                                // comment length
   }

   //--- XML escape for cell text / sheet name. Excel rejects raw < > & in
   //--- character data; ' and " are required inside attribute values which
   //--- we don't emit, but include for safety.
   std::string XmlEscape(const std::string& s)
   {
      std::string out; out.reserve(s.size());
      for(char c : s)
      {
         switch(c)
         {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            case '\'':out += "&apos;"; break;
            default:
               //--- Strip control chars Excel will reject (keep \t \r \n).
               if((unsigned char)c < 0x20 && c != '\t' && c != '\r' && c != '\n')
                  break;
               out += c;
         }
      }
      return out;
   }

   //--- A1 / B1 / ... / Z1 / AA1 / AB1 cell reference.
   std::string ColRef(size_t col_idx, size_t row_one_based)
   {
      std::string letters;
      size_t n = col_idx + 1;
      while(n > 0)
      {
         --n;
         letters = char('A' + (n % 26)) + letters;
         n /= 26;
      }
      char buf[16]; std::snprintf(buf, sizeof(buf), "%zu", row_one_based);
      return letters + buf;
   }

   //--- The five XML parts a minimal xlsx needs. We string-build because
   //--- they're tiny and ad-hoc; a full DOM lib would be massively overkill.

   std::string MakeContentTypes()
   {
      return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
             "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
             "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
             "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
             "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
             "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
             "</Types>";
   }

   std::string MakeRootRels()
   {
      return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
             "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
             "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
             "</Relationships>";
   }

   std::string MakeWorkbookXml(const std::string& sheet_name)
   {
      std::string out =
         "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
         "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
         "<sheets><sheet name=\"";
      out += XmlEscape(sheet_name);
      out += "\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
      return out;
   }

   std::string MakeWorkbookRels()
   {
      return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
             "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
             "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
             "</Relationships>";
   }

   std::string MakeSheetXml(const std::vector<std::string>& headers,
                            const std::vector<std::vector<XlsxWriter::Cell>>& rows)
   {
      std::ostringstream s;
      s << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        << "<sheetData>";

      //--- Header row (always inline strings, row 1).
      if(!headers.empty())
      {
         s << "<row r=\"1\">";
         for(size_t c = 0; c < headers.size(); ++c)
         {
            s << "<c r=\"" << ColRef(c, 1) << "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">"
              << XmlEscape(headers[c])
              << "</t></is></c>";
         }
         s << "</row>";
      }

      //--- Data rows start at row 2.
      for(size_t i = 0; i < rows.size(); ++i)
      {
         const size_t r1 = i + 2;
         s << "<row r=\"" << r1 << "\">";
         const auto& row = rows[i];
         for(size_t c = 0; c < row.size(); ++c)
         {
            const auto& cell = row[c];
            //--- Skip truly empty cells; Excel renders a blank by absence.
            if(cell.text.empty()) continue;
            s << "<c r=\"" << ColRef(c, r1) << "\"";
            if(cell.is_number)
            {
               //--- t="n" is the default but we set it explicitly so the
               //--- file reads identically across Excel/LibreOffice/Numbers.
               s << " t=\"n\"><v>" << cell.text << "</v></c>";
            }
            else
            {
               s << " t=\"inlineStr\"><is><t xml:space=\"preserve\">"
                 << XmlEscape(cell.text)
                 << "</t></is></c>";
            }
         }
         s << "</row>";
      }
      s << "</sheetData></worksheet>";
      return s.str();
   }
}

std::string XlsxWriter::Build(const std::string& sheet_name,
                              const std::vector<std::string>& headers,
                              const std::vector<std::vector<Cell>>& rows)
{
   std::string zip;
   std::vector<ZipEntry> entries;

   //--- Order matters only in that all local file headers must precede the
   //--- central directory — names are otherwise free.
   entries.push_back(WriteZipEntry(zip, "[Content_Types].xml",          MakeContentTypes()));
   entries.push_back(WriteZipEntry(zip, "_rels/.rels",                  MakeRootRels()));
   entries.push_back(WriteZipEntry(zip, "xl/workbook.xml",              MakeWorkbookXml(sheet_name)));
   entries.push_back(WriteZipEntry(zip, "xl/_rels/workbook.xml.rels",   MakeWorkbookRels()));
   entries.push_back(WriteZipEntry(zip, "xl/worksheets/sheet1.xml",     MakeSheetXml(headers, rows)));

   FinishZip(zip, entries);
   return zip;
}
