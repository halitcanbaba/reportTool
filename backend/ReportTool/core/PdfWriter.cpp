//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       PdfWriter.cpp              |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "PdfWriter.h"
#include <cstdio>
#include <sstream>
#include <algorithm>

namespace
{
   //--- A4 landscape in PDF points (1/72"): 842 × 595.
   constexpr double kPageW   = 842.0;
   constexpr double kPageH   = 595.0;
   constexpr double kMargin  = 30.0;
   constexpr double kRowH    = 12.0;
   constexpr double kTitleH  = 22.0;
   constexpr double kHeaderH = 16.0;
   constexpr double kFontSz  = 9.0;
   constexpr double kCharW   = kFontSz * 0.6;   // Courier is monospaced: 0.6 em width

   //--- PDF string escape for () \ and non-printables. We only emit ASCII
   //--- so multi-byte sequences (utf-8) round-trip as raw bytes — Courier
   //--- maps high-byte glyphs to MacRoman / WinAnsi, which is good enough
   //--- for report cells that are mostly ASCII anyway.
   std::string PdfEscape(const std::string& s)
   {
      std::string out; out.reserve(s.size() + 4);
      for(char c : s)
      {
         if(c == '(' || c == ')' || c == '\\') { out += '\\'; out += c; }
         else if((unsigned char)c < 0x20)       { /* skip control chars */ }
         else                                    out += c;
      }
      return out;
   }

   //--- Truncate each cell to a per-column character cap derived from the
   //--- available width. Long values get an ellipsis "…" replaced with "."
   //--- to stay in the WinAnsi safe range.
   std::vector<size_t> ChooseColWidths(size_t n_cols, double avail_chars)
   {
      std::vector<size_t> w(n_cols, 0);
      if(n_cols == 0) return w;
      const size_t base = (size_t)(avail_chars / (double)n_cols);
      for(auto& v : w) v = std::max<size_t>(6, base);
      return w;
   }

   std::string Pad(const std::string& s, size_t w)
   {
      if(s.size() >= w) return s.substr(0, w > 1 ? w - 1 : 0) + std::string(w > 1 ? 1 : 0, '.');
      return s + std::string(w - s.size(), ' ');
   }

   //--- PDF "object" accumulator. Reserve() returns an object id without
   //--- writing anything; Fill() commits the contents at the recorded
   //--- offset. This separation lets forward references (a page dict
   //--- naming its Pages parent before the parent itself exists) resolve
   //--- WITHOUT post-write string substitution — substitution would shift
   //--- bytes after the fact and corrupt every xref offset that follows.
   struct PdfBuilder {
      std::string body;
      std::vector<size_t> offsets;     // index i = byte offset of object (i+1) in body
      std::vector<bool>   committed;   // matches offsets[]; false when slot is just reserved

      int Reserve()
      {
         offsets.push_back(0);
         committed.push_back(false);
         return (int)offsets.size();
      }

      void Fill(int id, const std::string& obj)
      {
         const size_t idx = (size_t)id - 1;
         offsets[idx] = body.size();
         committed[idx] = true;
         char hdr[32]; std::snprintf(hdr, sizeof(hdr), "%d 0 obj\n", id);
         body += hdr;
         body += obj;
         body += "\nendobj\n";
      }

      int Add(const std::string& obj)
      {
         const int id = Reserve();
         Fill(id, obj);
         return id;
      }

      //--- Build a content-stream object. /Length is the EXACT byte count
      //--- between the LF that closes "stream\n" and the byte right before
      //--- "endstream" — i.e. content.size() with no extra trailing
      //--- newline. Off-by-one here is the difference between Acrobat
      //--- opening the file and refusing it as malformed.
      int AddStream(const std::string& content)
      {
         char head[64]; std::snprintf(head, sizeof(head), "<< /Length %zu >>\nstream\n", content.size());
         std::string obj = head;
         obj += content;
         obj += "endstream";
         return Add(obj);
      }

      //--- Final PDF: header + body + xref + trailer + startxref.
      std::string Finish(int catalog_id)
      {
         std::string pdf = "%PDF-1.4\n";
         pdf += "%\xC4\xC5\xC6\xC7\n";   // 4 binary bytes → mark as binary
         const size_t body_off = pdf.size();
         pdf += body;

         //--- xref table: 20-char records, "OOOOOOOOOO GGGGG n \n" each.
         const size_t xref_off = pdf.size();
         char xhdr[64]; std::snprintf(xhdr, sizeof(xhdr), "xref\n0 %zu\n", offsets.size() + 1);
         pdf += xhdr;
         pdf += "0000000000 65535 f \n";
         for(size_t o : offsets)
         {
            char rec[32]; std::snprintf(rec, sizeof(rec), "%010zu 00000 n \n", body_off + o);
            pdf += rec;
         }

         char trail[128];
         std::snprintf(trail, sizeof(trail),
                       "trailer\n<< /Size %zu /Root %d 0 R >>\nstartxref\n%zu\n%%%%EOF\n",
                       offsets.size() + 1, catalog_id, xref_off);
         pdf += trail;
         return pdf;
      }
   };

   //--- Emit one row's PDF text-show operator (Tj). All cells joined with
   //--- two spaces, columns pre-padded so Courier monospace aligns nicely.
   std::string RowText(const std::vector<std::string>& cells,
                       const std::vector<size_t>& widths)
   {
      std::string joined;
      for(size_t i = 0; i < cells.size() && i < widths.size(); ++i)
      {
         if(i > 0) joined += "  ";
         joined += Pad(cells[i], widths[i]);
      }
      return joined;
   }
}

std::string PdfWriter::Build(const std::string& title,
                             const std::vector<std::string>& headers,
                             const std::vector<std::vector<std::string>>& rows)
{
   PdfBuilder b;

   //--- Reserve the Pages parent object id BEFORE we write any page dicts.
   //--- Each page dict's /Parent ref then bakes in the real id directly
   //--- instead of a placeholder that would need post-write substitution
   //--- (which shifts byte offsets and breaks the xref table).
   const int pages_id   = b.Reserve();
   const int helv_b_id  = b.Add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>");
   const int courier_id = b.Add("<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>");

   //--- Available chars per row given the monospace width.
   const double avail_w = kPageW - 2 * kMargin;
   const size_t avail_chars = (size_t)(avail_w / kCharW);
   const std::vector<size_t> widths = ChooseColWidths(headers.size(), (double)avail_chars);

   //--- Rows per page after subtracting title + header line.
   const double rows_avail_y = kPageH - 2 * kMargin - kTitleH - kHeaderH;
   const size_t rows_per_page = std::max<size_t>(1, (size_t)(rows_avail_y / kRowH));

   //--- Build each page's content stream + page dict. We accumulate IDs as
   //--- we go so the Pages parent can list them.
   std::vector<int> page_ids;
   const size_t n_pages = (rows.size() + rows_per_page - 1) / rows_per_page;
   const size_t n_pages_final = n_pages == 0 ? 1 : n_pages;

   for(size_t pg = 0; pg < n_pages_final; ++pg)
   {
      //--- Build the page's drawing stream.
      std::ostringstream s;
      //--- Title line, Helvetica-Bold 14.
      s << "BT /F1 14 Tf "
        << (size_t)kMargin << " " << (size_t)(kPageH - kMargin - 14) << " Td "
        << "(" << PdfEscape(title);
      if(n_pages_final > 1) s << "  (page " << (pg + 1) << " / " << n_pages_final << ")";
      s << ") Tj ET\n";

      //--- Header row, Helvetica-Bold 9.
      const double header_y = kPageH - kMargin - kTitleH - 4;
      s << "BT /F1 " << (int)kFontSz << " Tf "
        << (size_t)kMargin << " " << (size_t)header_y << " Td "
        << "(" << PdfEscape(RowText(headers, widths)) << ") Tj ET\n";

      //--- Data rows in Courier 9 — Td(0, -kRowH) at the top of each line
      //--- after positioning the first row.
      const double first_row_y = header_y - kHeaderH;
      bool first = true;
      const size_t start = pg * rows_per_page;
      const size_t end   = std::min(rows.size(), start + rows_per_page);
      if(start < end)
      {
         s << "BT /F2 " << (int)kFontSz << " Tf "
           << (size_t)kMargin << " " << (size_t)first_row_y << " Td\n";
         for(size_t i = start; i < end; ++i)
         {
            if(!first) s << "0 -" << (int)kRowH << " Td\n";
            first = false;
            s << "(" << PdfEscape(RowText(rows[i], widths)) << ") Tj\n";
         }
         s << "ET\n";
      }

      const int contents_id = b.AddStream(s.str());

      //--- Page dict with the pre-reserved pages_id baked in directly.
      char dict[512];
      std::snprintf(dict, sizeof(dict),
         "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] "
         "/Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> "
         "/Contents %d 0 R >>",
         pages_id, (int)kPageW, (int)kPageH, helv_b_id, courier_id, contents_id);
      const int page_id = b.Add(dict);
      page_ids.push_back(page_id);
   }

   //--- Fill the reserved Pages parent now that all kids are known.
   std::ostringstream kids;
   for(size_t i = 0; i < page_ids.size(); ++i)
   {
      if(i > 0) kids << " ";
      kids << page_ids[i] << " 0 R";
   }
   char pages_dict[512];
   std::snprintf(pages_dict, sizeof(pages_dict),
      "<< /Type /Pages /Kids [%s] /Count %zu >>",
      kids.str().c_str(), page_ids.size());
   b.Fill(pages_id, pages_dict);

   //--- Catalog object — last so its xref offset is the highest.
   char cat[64];
   std::snprintf(cat, sizeof(cat), "<< /Type /Catalog /Pages %d 0 R >>", pages_id);
   const int catalog_id = b.Add(cat);

   return b.Finish(catalog_id);
}
