#include "../include/fs_alias.hpp"
#include "../include/system_tools.hpp"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <random>
#include <set>
#include <sstream>

using namespace std;
using json = nlohmann::json;

namespace MCP {

// Helper to write XML document using libxml2
static bool writeXmlDocument(const fs::path &path, xmlDocPtr doc) {
  fs::create_directories(path.parent_path());
  int bytes = xmlSaveFormatFileEnc(path.string().c_str(), doc, "UTF-8", 1);
  xmlFreeDoc(doc);
  return bytes >= 0;
}

// Helper to write raw content to a file
static bool writeRawFile(const fs::path &path, const string &content) {
  fs::create_directories(path.parent_path());
  ofstream file(path, ios::out | ios::binary);
  if (!file.is_open())
    return false;
  file << content;
  return true;
}

// Helper to generate a unique temp folder path inside the workspace
static fs::path createTempDir() {
  random_device rd;
  mt19937 gen(rd());
  uniform_int_distribution<> dis(100000, 999999);
  fs::path temp =
      fs::current_path() / (".mcp_office_temp_" + to_string(dis(gen)));
  fs::create_directories(temp);
  return temp;
}

// Helper to package the temp directory into a zip archive and remove temp
// directory
static bool packageZip(const fs::path &tempDir, const string &outputPath,
                       const vector<string> &foldersToZip) {
  fs::path absOutputPath = fs::absolute(fs::path(outputPath));

  // Ensure parent directory of output path exists
  fs::create_directories(absOutputPath.parent_path());

  // Build command
  stringstream ss;
  ss << "cd " << tempDir.string() << " && zip -q -r \""
     << absOutputPath.string() << "\"";
  for (const auto &f : foldersToZip) {
    ss << " " << f;
  }

  int ret = std::system(ss.str().c_str());

  // Cleanup temp dir
  try {
    fs::remove_all(tempDir);
  } catch (...) {
    // ignore errors during cleanup
  }

  return ret == 0;
}

// Helper to get image file extension
static string getFileExtension(const string &url) {
  size_t dotPos = url.find_last_of('.');
  if (dotPos == string::npos)
    return "png";
  string ext = url.substr(dotPos + 1);
  size_t qPos = ext.find_first_of("?#");
  if (qPos != string::npos) {
    ext = ext.substr(0, qPos);
  }
  transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif") {
    return ext;
  }
  return "png";
}

// Helper to download image using curl
static bool downloadImage(const string &url, const fs::path &destPath) {
  fs::create_directories(destPath.parent_path());
  string cmd = "curl -s -L -o \"" + destPath.string() + "\" \"" + url + "\"";
  int ret = std::system(cmd.c_str());
  return ret == 0 && fs::exists(destPath) && fs::file_size(destPath) > 0;
}

// Helper to convert column index to Excel column letters (A, B, C... AA, AB...)
static string getExcelColumnName(int colNum) {
  string columnName = "";
  while (colNum > 0) {
    int modulo = (colNum - 1) % 26;
    columnName = string(1, 'A' + modulo) + columnName;
    colNum = (colNum - modulo) / 26;
  }
  return columnName;
}

// -------------------- Word Document Generator --------------------
static bool generateWord(const fs::path &tempDir, const json &config) {
  std::set<string> imageExtensions;
  std::vector<std::pair<int, string>> imageRelationships;
  int imgCount = 1;

  // 1. Create _rels/.rels
  string rels =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships "
      "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
      "relationships\">\n"
      "  <Relationship Id=\"rId1\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships/officeDocument\" Target=\"word/document.xml\"/>\n"
      "</Relationships>";
  if (!writeRawFile(tempDir / "_rels" / ".rels", rels))
    return false;

  // 2. Create word/document.xml using libxml2
  xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr documentNode = xmlNewNode(nullptr, BAD_CAST "document");
  xmlDocSetRootElement(doc, documentNode);

  xmlNsPtr nsW = xmlNewNs(
      documentNode,
      BAD_CAST "http://schemas.openxmlformats.org/wordprocessingml/2006/main",
      BAD_CAST "w");
  xmlNsPtr nsR = xmlNewNs(
      documentNode,
      BAD_CAST
      "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
      BAD_CAST "r");
  xmlNsPtr nsWp =
      xmlNewNs(documentNode,
               BAD_CAST "http://schemas.openxmlformats.org/wordprocessingml/"
                        "2006/wordprocessingDrawing",
               BAD_CAST "wp");
  xmlNsPtr nsA =
      xmlNewNs(documentNode,
               BAD_CAST "http://schemas.openxmlformats.org/drawingml/2006/main",
               BAD_CAST "a");
  xmlNsPtr nsPic = xmlNewNs(
      documentNode,
      BAD_CAST "http://schemas.openxmlformats.org/drawingml/2006/picture",
      BAD_CAST "pic");

  xmlSetNs(documentNode, nsW);

  xmlNodePtr bodyNode =
      xmlNewChild(documentNode, nsW, BAD_CAST "body", nullptr);

  // Add paragraphs from config
  if (config.contains("paragraphs") && config["paragraphs"].is_array()) {
    for (const auto &p : config["paragraphs"]) {
      string text = p.value("text", "");
      string align = p.value("align", "left");
      bool bold = p.value("bold", false);
      bool italic = p.value("italic", false);
      int font_size = p.value("font_size", 11);
      string color = p.value("color", "");
      string heading = p.value("heading", "normal");
      int spacing_after = p.value("spacing_after", -1);
      string image_url = p.value("image_url", "");
      int image_width = p.value("image_width", 300);
      int image_height = p.value("image_height", 200);

      // Handle image if URL is provided
      if (!image_url.empty()) {
        string ext = getFileExtension(image_url);
        string imgFileName = "image" + to_string(imgCount) + "." + ext;
        fs::path destPath = tempDir / "word" / "media" / imgFileName;

        if (downloadImage(image_url, destPath)) {
          imageExtensions.insert(ext);
          imageRelationships.push_back({imgCount, "media/" + imgFileName});

          xmlNodePtr pNode = xmlNewChild(bodyNode, nsW, BAD_CAST "p", nullptr);

          // Center align the image
          xmlNodePtr pPrNode = xmlNewChild(pNode, nsW, BAD_CAST "pPr", nullptr);
          xmlNodePtr jcNode = xmlNewChild(pPrNode, nsW, BAD_CAST "jc", nullptr);
          xmlNewNsProp(jcNode, nsW, BAD_CAST "val", BAD_CAST "center");

          xmlNodePtr rNode = xmlNewChild(pNode, nsW, BAD_CAST "r", nullptr);
          xmlNodePtr drawingNode =
              xmlNewChild(rNode, nsW, BAD_CAST "drawing", nullptr);
          xmlNodePtr inlineNode =
              xmlNewChild(drawingNode, nsWp, BAD_CAST "inline", nullptr);

          int emuWidth = image_width * 9525;
          int emuHeight = image_height * 9525;

          xmlNodePtr extentNode =
              xmlNewChild(inlineNode, nsWp, BAD_CAST "extent", nullptr);
          xmlNewProp(extentNode, BAD_CAST "cx",
                     BAD_CAST to_string(emuWidth).c_str());
          xmlNewProp(extentNode, BAD_CAST "cy",
                     BAD_CAST to_string(emuHeight).c_str());

          xmlNodePtr docPrNode =
              xmlNewChild(inlineNode, nsWp, BAD_CAST "docPr", nullptr);
          xmlNewProp(docPrNode, BAD_CAST "id",
                     BAD_CAST to_string(imgCount).c_str());
          xmlNewProp(docPrNode, BAD_CAST "name",
                     BAD_CAST("Image" + to_string(imgCount)).c_str());

          xmlNewChild(inlineNode, nsWp, BAD_CAST "cNvGraphicFramePr", nullptr);

          xmlNodePtr graphicNode =
              xmlNewChild(inlineNode, nsA, BAD_CAST "graphic", nullptr);
          xmlNodePtr gdNode =
              xmlNewChild(graphicNode, nsA, BAD_CAST "graphicData", nullptr);
          xmlNewProp(
              gdNode, BAD_CAST "uri",
              BAD_CAST
              "http://schemas.openxmlformats.org/drawingml/2006/picture");

          xmlNodePtr picNode =
              xmlNewChild(gdNode, nsPic, BAD_CAST "pic", nullptr);
          xmlNodePtr nvPicPrNode =
              xmlNewChild(picNode, nsPic, BAD_CAST "nvPicPr", nullptr);
          xmlNodePtr cNvPrNode =
              xmlNewChild(nvPicPrNode, nsPic, BAD_CAST "cNvPr", nullptr);
          xmlNewProp(cNvPrNode, BAD_CAST "id", BAD_CAST "0");
          xmlNewProp(cNvPrNode, BAD_CAST "name",
                     BAD_CAST("image" + to_string(imgCount)).c_str());
          xmlNewChild(nvPicPrNode, nsPic, BAD_CAST "cNvPicPr", nullptr);

          xmlNodePtr blipFillNode =
              xmlNewChild(picNode, nsPic, BAD_CAST "blipFill", nullptr);
          xmlNodePtr blipNode =
              xmlNewChild(blipFillNode, nsA, BAD_CAST "blip", nullptr);
          xmlNewNsProp(blipNode, nsR, BAD_CAST "embed",
                       BAD_CAST("rIdImg" + to_string(imgCount)).c_str());

          xmlNodePtr stretchNode =
              xmlNewChild(blipFillNode, nsA, BAD_CAST "stretch", nullptr);
          xmlNewChild(stretchNode, nsA, BAD_CAST "fillRect", nullptr);

          xmlNodePtr spPrNode =
              xmlNewChild(picNode, nsPic, BAD_CAST "spPr", nullptr);
          xmlNodePtr xfrmNode =
              xmlNewChild(spPrNode, nsA, BAD_CAST "xfrm", nullptr);

          xmlNodePtr offNode =
              xmlNewChild(xfrmNode, nsA, BAD_CAST "off", nullptr);
          xmlNewProp(offNode, BAD_CAST "x", BAD_CAST "0");
          xmlNewProp(offNode, BAD_CAST "y", BAD_CAST "0");

          xmlNodePtr extNode =
              xmlNewChild(xfrmNode, nsA, BAD_CAST "ext", nullptr);
          xmlNewProp(extNode, BAD_CAST "cx",
                     BAD_CAST to_string(emuWidth).c_str());
          xmlNewProp(extNode, BAD_CAST "cy",
                     BAD_CAST to_string(emuHeight).c_str());

          xmlNodePtr prstGeomNode =
              xmlNewChild(spPrNode, nsA, BAD_CAST "prstGeom", nullptr);
          xmlNewProp(prstGeomNode, BAD_CAST "prst", BAD_CAST "rect");

          imgCount++;

          // If text is also provided, add it as a caption underneath the image
          if (!text.empty()) {
            xmlNodePtr capNode =
                xmlNewChild(bodyNode, nsW, BAD_CAST "p", nullptr);
            xmlNodePtr capPrNode =
                xmlNewChild(capNode, nsW, BAD_CAST "pPr", nullptr);
            xmlNodePtr capJcNode =
                xmlNewChild(capPrNode, nsW, BAD_CAST "jc", nullptr);
            xmlNewNsProp(capJcNode, nsW, BAD_CAST "val", BAD_CAST "center");

            // Extra space after caption
            xmlNodePtr capSpacingNode =
                xmlNewChild(capPrNode, nsW, BAD_CAST "spacing", nullptr);
            xmlNewNsProp(capSpacingNode, nsW, BAD_CAST "after", BAD_CAST "240");

            xmlNodePtr capRNode =
                xmlNewChild(capNode, nsW, BAD_CAST "r", nullptr);
            xmlNodePtr capRPrNode =
                xmlNewChild(capRNode, nsW, BAD_CAST "rPr", nullptr);
            xmlNewChild(capRPrNode, nsW, BAD_CAST "i", nullptr); // italic

            xmlNodePtr szNode =
                xmlNewChild(capRPrNode, nsW, BAD_CAST "sz", nullptr);
            xmlNewNsProp(szNode, nsW, BAD_CAST "val",
                         BAD_CAST "18"); // 9pt font size

            xmlNewChild(capRNode, nsW, BAD_CAST "t", BAD_CAST text.c_str());
          }
          continue; // Move to next paragraph
        } else {
          // If download failed, render text notification instead of failing
          text = "[Warning: Image could not be loaded from " + image_url + "]" +
                 (text.empty() ? "" : " - " + text);
          bold = true;
          color = "FF0000";
        }
      }

      // Apply heading defaults if heading is set
      if (heading == "h1") {
        bold = true;
        font_size = 24;
        if (spacing_after == -1)
          spacing_after = 240;
      } else if (heading == "h2") {
        bold = true;
        font_size = 18;
        if (spacing_after == -1)
          spacing_after = 180;
      } else if (heading == "h3") {
        bold = true;
        font_size = 14;
        if (spacing_after == -1)
          spacing_after = 120;
      } else {
        if (spacing_after == -1)
          spacing_after = 120;
      }

      xmlNodePtr pNode = xmlNewChild(bodyNode, nsW, BAD_CAST "p", nullptr);

      // Paragraph properties (alignment, spacing)
      xmlNodePtr pPrNode = xmlNewChild(pNode, nsW, BAD_CAST "pPr", nullptr);
      if (align != "left") {
        xmlNodePtr jcNode = xmlNewChild(pPrNode, nsW, BAD_CAST "jc", nullptr);
        xmlNewNsProp(jcNode, nsW, BAD_CAST "val", BAD_CAST align.c_str());
      }
      if (spacing_after >= 0) {
        xmlNodePtr spacingNode =
            xmlNewChild(pPrNode, nsW, BAD_CAST "spacing", nullptr);
        xmlNewNsProp(spacingNode, nsW, BAD_CAST "after",
                     BAD_CAST to_string(spacing_after).c_str());
      }

      // Run
      xmlNodePtr rNode = xmlNewChild(pNode, nsW, BAD_CAST "r", nullptr);

      // Run properties (bold, italic, font size, color)
      xmlNodePtr rPrNode = xmlNewChild(rNode, nsW, BAD_CAST "rPr", nullptr);
      if (bold) {
        xmlNewChild(rPrNode, nsW, BAD_CAST "b", nullptr);
      }
      if (italic) {
        xmlNewChild(rPrNode, nsW, BAD_CAST "i", nullptr);
      }
      if (font_size > 0) {
        xmlNodePtr szNode = xmlNewChild(rPrNode, nsW, BAD_CAST "sz", nullptr);
        xmlNewNsProp(szNode, nsW, BAD_CAST "val",
                     BAD_CAST to_string(font_size * 2).c_str()); // half-points
      }
      if (!color.empty()) {
        xmlNodePtr colorNode =
            xmlNewChild(rPrNode, nsW, BAD_CAST "color", nullptr);
        xmlNewNsProp(colorNode, nsW, BAD_CAST "val", BAD_CAST color.c_str());
      }

      // Text
      xmlNewChild(rNode, nsW, BAD_CAST "t", BAD_CAST text.c_str());
    }
  } else {
    // Add at least one paragraph so it's a valid document
    xmlNodePtr pNode = xmlNewChild(bodyNode, nsW, BAD_CAST "p", nullptr);
    xmlNodePtr rNode = xmlNewChild(pNode, nsW, BAD_CAST "r", nullptr);
    xmlNewChild(rNode, nsW, BAD_CAST "t", BAD_CAST "Hello World");
  }

  // Margins and section properties
  double top_margin = 1.0;
  double bottom_margin = 1.0;
  double left_margin = 1.0;
  double right_margin = 1.0;

  if (config.contains("margins") && config["margins"].is_object()) {
    const auto &m = config["margins"];
    top_margin = m.value("top", 1.0);
    bottom_margin = m.value("bottom", 1.0);
    left_margin = m.value("left", 1.0);
    right_margin = m.value("right", 1.0);
  }

  int top_twips = static_cast<int>(top_margin * 1440);
  int bottom_twips = static_cast<int>(bottom_margin * 1440);
  int left_twips = static_cast<int>(left_margin * 1440);
  int right_twips = static_cast<int>(right_margin * 1440);

  xmlNodePtr sectPrNode =
      xmlNewChild(bodyNode, nsW, BAD_CAST "sectPr", nullptr);
  xmlNodePtr pgMarNode =
      xmlNewChild(sectPrNode, nsW, BAD_CAST "pgMar", nullptr);
  xmlNewNsProp(pgMarNode, nsW, BAD_CAST "top",
               BAD_CAST to_string(top_twips).c_str());
  xmlNewNsProp(pgMarNode, nsW, BAD_CAST "bottom",
               BAD_CAST to_string(bottom_twips).c_str());
  xmlNewNsProp(pgMarNode, nsW, BAD_CAST "left",
               BAD_CAST to_string(left_twips).c_str());
  xmlNewNsProp(pgMarNode, nsW, BAD_CAST "right",
               BAD_CAST to_string(right_twips).c_str());

  if (!writeXmlDocument(tempDir / "word" / "document.xml", doc))
    return false;

  // 3. Generate dynamic [Content_Types].xml
  xmlDocPtr typesDoc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr typesRoot = xmlNewNode(nullptr, BAD_CAST "Types");
  xmlDocSetRootElement(typesDoc, typesRoot);
  xmlNewNs(typesRoot,
           BAD_CAST
           "http://schemas.openxmlformats.org/package/2006/content-types",
           nullptr);

  xmlNodePtr relsDefault =
      xmlNewChild(typesRoot, nullptr, BAD_CAST "Default", nullptr);
  xmlNewProp(relsDefault, BAD_CAST "Extension", BAD_CAST "rels");
  xmlNewProp(relsDefault, BAD_CAST "ContentType",
             BAD_CAST
             "application/vnd.openxmlformats-package.relationships+xml");

  xmlNodePtr xmlDefault =
      xmlNewChild(typesRoot, nullptr, BAD_CAST "Default", nullptr);
  xmlNewProp(xmlDefault, BAD_CAST "Extension", BAD_CAST "xml");
  xmlNewProp(xmlDefault, BAD_CAST "ContentType", BAD_CAST "application/xml");

  // Add default image types dynamically
  for (const auto &ext : imageExtensions) {
    xmlNodePtr imgDefault =
        xmlNewChild(typesRoot, nullptr, BAD_CAST "Default", nullptr);
    xmlNewProp(imgDefault, BAD_CAST "Extension", BAD_CAST ext.c_str());
    string ct = "image/" + (ext == "jpg" ? "jpeg" : ext);
    xmlNewProp(imgDefault, BAD_CAST "ContentType", BAD_CAST ct.c_str());
  }

  xmlNodePtr docOverride =
      xmlNewChild(typesRoot, nullptr, BAD_CAST "Override", nullptr);
  xmlNewProp(docOverride, BAD_CAST "PartName", BAD_CAST "/word/document.xml");
  xmlNewProp(
      docOverride, BAD_CAST "ContentType",
      BAD_CAST
      "application/"
      "vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml");

  if (!writeXmlDocument(tempDir / "[Content_Types].xml", typesDoc))
    return false;

  // 4. Generate dynamic word/_rels/document.xml.rels
  xmlDocPtr docRelsDoc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr docRelsRoot = xmlNewNode(nullptr, BAD_CAST "Relationships");
  xmlDocSetRootElement(docRelsDoc, docRelsRoot);
  xmlNewNs(docRelsRoot,
           BAD_CAST
           "http://schemas.openxmlformats.org/package/2006/relationships",
           nullptr);

  for (const auto &rel : imageRelationships) {
    xmlNodePtr relNode =
        xmlNewChild(docRelsRoot, nullptr, BAD_CAST "Relationship", nullptr);
    xmlNewProp(relNode, BAD_CAST "Id",
               BAD_CAST("rIdImg" + to_string(rel.first)).c_str());
    xmlNewProp(relNode, BAD_CAST "Type",
               BAD_CAST "http://schemas.openxmlformats.org/officeDocument/2006/"
                        "relationships/image");
    xmlNewProp(relNode, BAD_CAST "Target", BAD_CAST rel.second.c_str());
  }

  if (!writeXmlDocument(tempDir / "word" / "_rels" / "document.xml.rels",
                        docRelsDoc))
    return false;

  return true;
}

// -------------------- Excel Spreadsheet Generator --------------------
static bool generateExcel(const fs::path &tempDir, const json &config) {
  json sheetsArray = json::array();
  if (config.contains("sheets") && config["sheets"].is_array()) {
    sheetsArray = config["sheets"];
  } else {
    json defSheet = {
        {"name", "Sheet1"},
        {"rows",
         json::array({json::array(
             {json::object({{"value", "Hello"}, {"type", "string"}}),
              json::object({{"value", "123.45"}, {"type", "number"}})})})}};
    sheetsArray.push_back(defSheet);
  }

  // 1. Generate [Content_Types].xml
  xmlDocPtr typesDoc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr typesRoot = xmlNewNode(nullptr, BAD_CAST "Types");
  xmlDocSetRootElement(typesDoc, typesRoot);
  xmlNewNs(typesRoot,
           BAD_CAST
           "http://schemas.openxmlformats.org/package/2006/content-types",
           nullptr);

  xmlNodePtr relsDefault =
      xmlNewChild(typesRoot, nullptr, BAD_CAST "Default", nullptr);
  xmlNewProp(relsDefault, BAD_CAST "Extension", BAD_CAST "rels");
  xmlNewProp(relsDefault, BAD_CAST "ContentType",
             BAD_CAST
             "application/vnd.openxmlformats-package.relationships+xml");

  xmlNodePtr xmlDefault =
      xmlNewChild(typesRoot, nullptr, BAD_CAST "Default", nullptr);
  xmlNewProp(xmlDefault, BAD_CAST "Extension", BAD_CAST "xml");
  xmlNewProp(xmlDefault, BAD_CAST "ContentType", BAD_CAST "application/xml");

  xmlNodePtr wbOverride =
      xmlNewChild(typesRoot, nullptr, BAD_CAST "Override", nullptr);
  xmlNewProp(wbOverride, BAD_CAST "PartName", BAD_CAST "/xl/workbook.xml");
  xmlNewProp(wbOverride, BAD_CAST "ContentType",
             BAD_CAST
             "application/"
             "vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");

  for (size_t i = 0; i < sheetsArray.size(); ++i) {
    xmlNodePtr sheetOverride =
        xmlNewChild(typesRoot, nullptr, BAD_CAST "Override", nullptr);
    string partName = "/xl/worksheets/sheet" + to_string(i + 1) + ".xml";
    xmlNewProp(sheetOverride, BAD_CAST "PartName", BAD_CAST partName.c_str());
    xmlNewProp(sheetOverride, BAD_CAST "ContentType",
               BAD_CAST
               "application/"
               "vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
  }
  if (!writeXmlDocument(tempDir / "[Content_Types].xml", typesDoc))
    return false;

  // 2. Generate _rels/.rels
  string rels =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships "
      "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
      "relationships\">\n"
      "  <Relationship Id=\"rId1\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships/officeDocument\" Target=\"xl/workbook.xml\"/>\n"
      "</Relationships>";
  if (!writeRawFile(tempDir / "_rels" / ".rels", rels))
    return false;

  // 3. Generate xl/_rels/workbook.xml.rels
  xmlDocPtr wbRelsDoc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr wbRelsRoot = xmlNewNode(nullptr, BAD_CAST "Relationships");
  xmlDocSetRootElement(wbRelsDoc, wbRelsRoot);
  xmlNewNs(wbRelsRoot,
           BAD_CAST
           "http://schemas.openxmlformats.org/package/2006/relationships",
           nullptr);

  for (size_t i = 0; i < sheetsArray.size(); ++i) {
    xmlNodePtr relNode =
        xmlNewChild(wbRelsRoot, nullptr, BAD_CAST "Relationship", nullptr);
    xmlNewProp(relNode, BAD_CAST "Id",
               BAD_CAST("rId" + to_string(i + 1)).c_str());
    xmlNewProp(relNode, BAD_CAST "Type",
               BAD_CAST "http://schemas.openxmlformats.org/officeDocument/2006/"
                        "relationships/worksheet");
    string target = "worksheets/sheet" + to_string(i + 1) + ".xml";
    xmlNewProp(relNode, BAD_CAST "Target", BAD_CAST target.c_str());
  }
  if (!writeXmlDocument(tempDir / "xl" / "_rels" / "workbook.xml.rels",
                        wbRelsDoc))
    return false;

  // 4. Generate xl/workbook.xml
  xmlDocPtr wbDoc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr wbRoot = xmlNewNode(nullptr, BAD_CAST "workbook");
  xmlDocSetRootElement(wbDoc, wbRoot);
  xmlNsPtr nsMain = xmlNewNs(
      wbRoot,
      BAD_CAST "http://schemas.openxmlformats.org/spreadsheetml/2006/main",
      nullptr);
  xmlNsPtr nsR = xmlNewNs(
      wbRoot,
      BAD_CAST
      "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
      BAD_CAST "r");

  xmlNodePtr sheetsNode =
      xmlNewChild(wbRoot, nsMain, BAD_CAST "sheets", nullptr);
  for (size_t i = 0; i < sheetsArray.size(); ++i) {
    string sheetName = sheetsArray[i].value("name", "Sheet" + to_string(i + 1));
    xmlNodePtr sheetNode =
        xmlNewChild(sheetsNode, nsMain, BAD_CAST "sheet", nullptr);
    xmlNewProp(sheetNode, BAD_CAST "name", BAD_CAST sheetName.c_str());
    xmlNewProp(sheetNode, BAD_CAST "sheetId",
               BAD_CAST to_string(i + 1).c_str());
    xmlNewNsProp(sheetNode, nsR, BAD_CAST "id",
                 BAD_CAST("rId" + to_string(i + 1)).c_str());
  }
  if (!writeXmlDocument(tempDir / "xl" / "workbook.xml", wbDoc))
    return false;

  // 5. Generate worksheets/sheetX.xml
  for (size_t s = 0; s < sheetsArray.size(); ++s) {
    xmlDocPtr wsDoc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr wsRoot = xmlNewNode(nullptr, BAD_CAST "worksheet");
    xmlDocSetRootElement(wsDoc, wsRoot);
    xmlNsPtr nsSheet = xmlNewNs(
        wsRoot,
        BAD_CAST "http://schemas.openxmlformats.org/spreadsheetml/2006/main",
        nullptr);

    xmlNodePtr sdNode =
        xmlNewChild(wsRoot, nsSheet, BAD_CAST "sheetData", nullptr);

    if (sheetsArray[s].contains("rows") && sheetsArray[s]["rows"].is_array()) {
      const auto &rows = sheetsArray[s]["rows"];
      for (size_t r = 0; r < rows.size(); ++r) {
        if (!rows[r].is_array())
          continue;
        xmlNodePtr rNode =
            xmlNewChild(sdNode, nsSheet, BAD_CAST "row", nullptr);
        xmlNewProp(rNode, BAD_CAST "r", BAD_CAST to_string(r + 1).c_str());

        const auto &rowData = rows[r];
        for (size_t c = 0; c < rowData.size(); ++c) {
          string cellVal = "";
          string cellType = "string";

          if (rowData[c].is_object()) {
            cellVal = rowData[c].value("value", "");
            cellType = rowData[c].value("type", "string");
          } else if (rowData[c].is_string()) {
            cellVal = rowData[c].get<string>();
          } else if (rowData[c].is_number()) {
            cellVal = rowData[c].dump();
            cellType = "number";
          }

          string cellRef = getExcelColumnName(c + 1) + to_string(r + 1);
          xmlNodePtr cNode = xmlNewChild(rNode, nsSheet, BAD_CAST "c", nullptr);
          xmlNewProp(cNode, BAD_CAST "r", BAD_CAST cellRef.c_str());

          if (cellType == "number") {
            xmlNewChild(cNode, nsSheet, BAD_CAST "v", BAD_CAST cellVal.c_str());
          } else {
            xmlNewProp(cNode, BAD_CAST "t", BAD_CAST "inlineStr");
            xmlNodePtr isNode =
                xmlNewChild(cNode, nsSheet, BAD_CAST "is", nullptr);
            xmlNewChild(isNode, nsSheet, BAD_CAST "t",
                        BAD_CAST cellVal.c_str());
          }
        }
      }
    }
    string sheetPath = "xl/worksheets/sheet" + to_string(s + 1) + ".xml";
    if (!writeXmlDocument(tempDir / sheetPath, wsDoc))
      return false;
  }

  return true;
}

// -------------------- PowerPoint Presentation Generator --------------------
static bool generatePowerPoint(const fs::path &tempDir, const json &config) {
  json slidesArray = json::array();
  if (config.contains("slides") && config["slides"].is_array()) {
    slidesArray = config["slides"];
  } else {
    json defSlide = {
        {"title", "Hello World"},
        {"bullets",
         json::array({"This is slide 1", "Generated by MCP Office Wizard"})}};
    slidesArray.push_back(defSlide);
  }

  // 1. Write basic static template files
  // Theme
  string themeXml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<a:theme "
      "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
      "name=\"Office Theme\">\n"
      "  <a:themeElements>\n"
      "    <a:clrScheme name=\"Office\">\n"
      "      <a:dk1><a:sysClr val=\"windowText\" lastClr=\"000000\"/></a:dk1>\n"
      "      <a:lt1><a:sysClr val=\"window\" lastClr=\"FFFFFF\"/></a:lt1>\n"
      "      <a:dk2><a:srgbClr val=\"44546A\"/></a:dk2>\n"
      "      <a:lt2><a:srgbClr val=\"E7E6E6\"/></a:lt2>\n"
      "      <a:accent1><a:srgbClr val=\"4472C4\"/></a:accent1>\n"
      "      <a:accent2><a:srgbClr val=\"ED7D31\"/></a:accent2>\n"
      "      <a:accent3><a:srgbClr val=\"A5A5A5\"/></a:accent3>\n"
      "      <a:accent4><a:srgbClr val=\"FFC000\"/></a:accent4>\n"
      "      <a:accent5><a:srgbClr val=\"5B9BD5\"/></a:accent5>\n"
      "      <a:accent6><a:srgbClr val=\"70AD47\"/></a:accent6>\n"
      "      <a:hlink><a:srgbClr val=\"0563C1\"/></a:hlink>\n"
      "      <a:folHlink><a:srgbClr val=\"954F72\"/></a:folHlink>\n"
      "    </a:clrScheme>\n"
      "    <a:fontScheme name=\"Office\">\n"
      "      <a:majorFont><a:latin typeface=\"Calibri Light\"/><a:ea "
      "typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont>\n"
      "      <a:minorFont><a:latin typeface=\"Calibri\"/><a:ea "
      "typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont>\n"
      "    </a:fontScheme>\n"
      "    <a:fmtScheme name=\"Office\">\n"
      "      <a:fillStyleLst>\n"
      "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n"
      "        <a:gradFill rotWithShape=\"1\">\n"
      "          <a:gsLst>\n"
      "            <a:gs pos=\"0\"><a:schemeClr val=\"phClr\"><a:tint "
      "val=\"50000\"/><a:satMod val=\"300000\"/></a:schemeClr></a:gs>\n"
      "            <a:gs pos=\"35000\"><a:schemeClr val=\"phClr\"><a:tint "
      "val=\"37000\"/><a:satMod val=\"300000\"/></a:schemeClr></a:gs>\n"
      "            <a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"><a:tint "
      "val=\"15000\"/><a:satMod val=\"350000\"/></a:schemeClr></a:gs>\n"
      "          </a:gsLst>\n"
      "          <a:lin ang=\"16200000\" scaled=\"1\"/>\n"
      "        </a:gradFill>\n"
      "        <a:gradFill rotWithShape=\"1\">\n"
      "          <a:gsLst>\n"
      "            <a:gs pos=\"0\"><a:schemeClr val=\"phClr\"><a:shade "
      "val=\"51000\"/><a:satMod val=\"130000\"/></a:schemeClr></a:gs>\n"
      "            <a:gs pos=\"80000\"><a:schemeClr val=\"phClr\"><a:shade "
      "val=\"93000\"/><a:satMod val=\"130000\"/></a:schemeClr></a:gs>\n"
      "            <a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"><a:shade "
      "val=\"94000\"/><a:satMod val=\"135000\"/></a:schemeClr></a:gs>\n"
      "          </a:gsLst>\n"
      "          <a:lin ang=\"16200000\" scaled=\"1\"/>\n"
      "        </a:gradFill>\n"
      "      </a:fillStyleLst>\n"
      "      <a:lnStyleLst>\n"
      "        <a:ln w=\"6350\" cmpd=\"s\"><a:solidFill><a:schemeClr "
      "val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>\n"
      "        <a:ln w=\"12700\" cmpd=\"s\"><a:solidFill><a:schemeClr "
      "val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>\n"
      "        <a:ln w=\"19050\" cmpd=\"s\"><a:solidFill><a:schemeClr "
      "val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>\n"
      "      </a:lnStyleLst>\n"
      "      <a:effectStyleLst>\n"
      "        <a:effectStyle><a:effectLst/></a:effectStyle>\n"
      "        <a:effectStyle><a:effectLst/></a:effectStyle>\n"
      "        <a:effectStyle><a:effectLst/></a:effectStyle>\n"
      "      </a:effectStyleLst>\n"
      "      <a:bgFillStyleLst>\n"
      "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n"
      "        <a:solidFill><a:schemeClr val=\"phClr\"><a:tint "
      "val=\"95000\"/></a:schemeClr></a:solidFill>\n"
      "        <a:gradFill rotWithShape=\"1\">\n"
      "          <a:gsLst>\n"
      "            <a:gs pos=\"0\"><a:schemeClr val=\"phClr\"><a:tint "
      "val=\"95000\"/></a:schemeClr></a:gs>\n"
      "            <a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"><a:tint "
      "val=\"90000\"/></a:schemeClr></a:gs>\n"
      "          </a:gsLst>\n"
      "          <a:lin ang=\"16200000\" scaled=\"1\"/>\n"
      "        </a:gradFill>\n"
      "      </a:bgFillStyleLst>\n"
      "    </a:fmtScheme>\n"
      "  </a:themeElements>\n"
      "</a:theme>";
  if (!writeRawFile(tempDir / "ppt" / "theme" / "theme1.xml", themeXml))
    return false;

  // Slide Layout
  string layoutXml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<p:sldLayout "
      "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
      "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships\" "
      "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" "
      "type=\"titleAndContent\" preserve=\"1\">\n"
      "  <p:cSld name=\"Title and Content\">\n"
      "    <p:spTree>\n"
      "      <p:nvGrpSpPr>\n"
      "        <p:cNvPr id=\"1\" name=\"\"/>\n"
      "        <p:cNvGrpSpPr/>\n"
      "        <p:nvPr/>\n"
      "      </p:nvGrpSpPr>\n"
      "      <p:grpSpPr/>\n"
      "    </p:spTree>\n"
      "  </p:cSld>\n"
      "</p:sldLayout>";
  if (!writeRawFile(tempDir / "ppt" / "slideLayouts" / "slideLayout1.xml",
                    layoutXml))
    return false;

  string layoutRelsXml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships "
      "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
      "relationships\">\n"
      "  <Relationship Id=\"rId1\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships/slideMaster\" "
      "Target=\"../slideMasters/slideMaster1.xml\"/>\n"
      "</Relationships>";
  if (!writeRawFile(tempDir / "ppt" / "slideLayouts" / "_rels" /
                        "slideLayout1.xml.rels",
                    layoutRelsXml))
    return false;

  // Slide Master
  string masterXml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<p:sldMaster "
      "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
      "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships\" "
      "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/"
      "main\">\n"
      "  <p:cSld>\n"
      "    <p:bg>\n"
      "      <p:bgPr>\n"
      "        <a:solidFill><a:schemeClr val=\"bg1\"/></a:solidFill>\n"
      "      </p:bgPr>\n"
      "    </p:bg>\n"
      "    <p:spTree>\n"
      "      <p:nvGrpSpPr>\n"
      "        <p:cNvPr id=\"1\" name=\"\"/>\n"
      "        <p:cNvGrpSpPr/>\n"
      "        <p:nvPr/>\n"
      "      </p:nvGrpSpPr>\n"
      "      <p:grpSpPr/>\n"
      "    </p:spTree>\n"
      "  </p:cSld>\n"
      "  <p:sldLayoutIdLst>\n"
      "    <p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/>\n"
      "  </p:sldLayoutIdLst>\n"
      "</p:sldMaster>";
  if (!writeRawFile(tempDir / "ppt" / "slideMasters" / "slideMaster1.xml",
                    masterXml))
    return false;

  string masterRelsXml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships "
      "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
      "relationships\">\n"
      "  <Relationship Id=\"rId1\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships/slideLayout\" "
      "Target=\"../slideLayouts/slideLayout1.xml\"/>\n"
      "  <Relationship Id=\"rId2\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships/theme\" Target=\"../theme/theme1.xml\"/>\n"
      "</Relationships>";
  if (!writeRawFile(tempDir / "ppt" / "slideMasters" / "_rels" /
                        "slideMaster1.xml.rels",
                    masterRelsXml))
    return false;

  // _rels/.rels
  string relsXml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships "
      "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
      "relationships\">\n"
      "  <Relationship Id=\"rId1\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships/officeDocument\" Target=\"ppt/presentation.xml\"/>\n"
      "</Relationships>";
  if (!writeRawFile(tempDir / "_rels" / ".rels", relsXml))
    return false;

  // 3. Generate [Content_Types].xml dynamically
  xmlDocPtr typesDoc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr typesRoot = xmlNewNode(nullptr, BAD_CAST "Types");
  xmlDocSetRootElement(typesDoc, typesRoot);
  xmlNewNs(typesRoot,
           BAD_CAST
           "http://schemas.openxmlformats.org/package/2006/content-types",
           nullptr);

  auto addDefault = [&](const string &ext, const string &ct) {
    xmlNodePtr node =
        xmlNewChild(typesRoot, nullptr, BAD_CAST "Default", nullptr);
    xmlNewProp(node, BAD_CAST "Extension", BAD_CAST ext.c_str());
    xmlNewProp(node, BAD_CAST "ContentType", BAD_CAST ct.c_str());
  };
  auto addOverride = [&](const string &pn, const string &ct) {
    xmlNodePtr node =
        xmlNewChild(typesRoot, nullptr, BAD_CAST "Override", nullptr);
    xmlNewProp(node, BAD_CAST "PartName", BAD_CAST pn.c_str());
    xmlNewProp(node, BAD_CAST "ContentType", BAD_CAST ct.c_str());
  };

  addDefault("rels",
             "application/vnd.openxmlformats-package.relationships+xml");
  addDefault("xml", "application/xml");
  addOverride(
      "/ppt/presentation.xml",
      "application/"
      "vnd.openxmlformats-officedocument.presentationml.presentation.main+xml");
  addOverride(
      "/ppt/slideLayouts/slideLayout1.xml",
      "application/"
      "vnd.openxmlformats-officedocument.presentationml.slideLayout+xml");
  addOverride(
      "/ppt/slideMasters/slideMaster1.xml",
      "application/"
      "vnd.openxmlformats-officedocument.presentationml.slideMaster+xml");
  addOverride("/ppt/theme/theme1.xml",
              "application/vnd.openxmlformats-officedocument.theme+xml");

  for (size_t i = 0; i < slidesArray.size(); ++i) {
    addOverride("/ppt/slides/slide" + to_string(i + 1) + ".xml",
                "application/"
                "vnd.openxmlformats-officedocument.presentationml.slide+xml");
  }

  if (!writeXmlDocument(tempDir / "[Content_Types].xml", typesDoc))
    return false;

  // 4. Generate ppt/_rels/presentation.xml.rels dynamically
  xmlDocPtr presRelsDoc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr presRelsRoot = xmlNewNode(nullptr, BAD_CAST "Relationships");
  xmlDocSetRootElement(presRelsDoc, presRelsRoot);
  xmlNewNs(presRelsRoot,
           BAD_CAST
           "http://schemas.openxmlformats.org/package/2006/relationships",
           nullptr);

  auto addRel = [&](const string &id, const string &type,
                    const string &target) {
    xmlNodePtr node =
        xmlNewChild(presRelsRoot, nullptr, BAD_CAST "Relationship", nullptr);
    xmlNewProp(node, BAD_CAST "Id", BAD_CAST id.c_str());
    xmlNewProp(node, BAD_CAST "Type", BAD_CAST type.c_str());
    xmlNewProp(node, BAD_CAST "Target", BAD_CAST target.c_str());
  };

  addRel("rIdMaster1",
         "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
         "slideMaster",
         "slideMasters/slideMaster1.xml");
  addRel("rIdTheme1",
         "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
         "theme",
         "theme/theme1.xml");

  for (size_t i = 0; i < slidesArray.size(); ++i) {
    addRel("rIdSlide" + to_string(i + 1),
           "http://schemas.openxmlformats.org/officeDocument/2006/"
           "relationships/slide",
           "slides/slide" + to_string(i + 1) + ".xml");
  }

  if (!writeXmlDocument(tempDir / "ppt" / "_rels" / "presentation.xml.rels",
                        presRelsDoc))
    return false;

  // 5. Generate ppt/presentation.xml dynamically
  xmlDocPtr presDoc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr presRoot = xmlNewNode(nullptr, BAD_CAST "presentation");
  xmlDocSetRootElement(presDoc, presRoot);
  xmlNsPtr nsP = xmlNewNs(
      presRoot,
      BAD_CAST "http://schemas.openxmlformats.org/presentationml/2006/main",
      BAD_CAST "p");
  xmlNsPtr nsA =
      xmlNewNs(presRoot,
               BAD_CAST "http://schemas.openxmlformats.org/drawingml/2006/main",
               BAD_CAST "a");
  xmlNsPtr nsR = xmlNewNs(
      presRoot,
      BAD_CAST
      "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
      BAD_CAST "r");
  xmlSetNs(presRoot, nsP);

  xmlNodePtr masterIdLstNode =
      xmlNewChild(presRoot, nsP, BAD_CAST "sldMasterIdLst", nullptr);
  xmlNodePtr masterIdNode =
      xmlNewChild(masterIdLstNode, nsP, BAD_CAST "sldMasterId", nullptr);
  xmlNewProp(masterIdNode, BAD_CAST "id", BAD_CAST "2147483648");
  xmlNewNsProp(masterIdNode, nsR, BAD_CAST "id", BAD_CAST "rIdMaster1");

  xmlNodePtr sldIdLstNode =
      xmlNewChild(presRoot, nsP, BAD_CAST "sldIdLst", nullptr);
  for (size_t i = 0; i < slidesArray.size(); ++i) {
    xmlNodePtr sldIdNode =
        xmlNewChild(sldIdLstNode, nsP, BAD_CAST "sldId", nullptr);
    xmlNewProp(sldIdNode, BAD_CAST "id", BAD_CAST to_string(256 + i).c_str());
    xmlNewNsProp(sldIdNode, nsR, BAD_CAST "id",
                 BAD_CAST("rIdSlide" + to_string(i + 1)).c_str());
  }

  xmlNodePtr sldSzNode = xmlNewChild(presRoot, nsP, BAD_CAST "sldSz", nullptr);
  xmlNewProp(sldSzNode, BAD_CAST "cx", BAD_CAST "9144000");
  xmlNewProp(sldSzNode, BAD_CAST "cy", BAD_CAST "6858000");

  xmlNodePtr notesSzNode =
      xmlNewChild(presRoot, nsP, BAD_CAST "notesSz", nullptr);
  xmlNewProp(notesSzNode, BAD_CAST "cx", BAD_CAST "6858000");
  xmlNewProp(notesSzNode, BAD_CAST "cy", BAD_CAST "9144000");

  if (!writeXmlDocument(tempDir / "ppt" / "presentation.xml", presDoc))
    return false;

  // 6. Generate individual slides and their relationship files
  for (size_t s = 0; s < slidesArray.size(); ++s) {
    string slideTitle =
        slidesArray[s].value("title", "Slide " + to_string(s + 1));

    xmlDocPtr slideDoc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr slideRoot = xmlNewNode(nullptr, BAD_CAST "sld");
    xmlDocSetRootElement(slideDoc, slideRoot);
    xmlNsPtr nsSldP = xmlNewNs(
        slideRoot,
        BAD_CAST "http://schemas.openxmlformats.org/presentationml/2006/main",
        BAD_CAST "p");
    xmlNsPtr nsSldA = xmlNewNs(
        slideRoot,
        BAD_CAST "http://schemas.openxmlformats.org/drawingml/2006/main",
        BAD_CAST "a");
    xmlNsPtr nsSldR = xmlNewNs(
        slideRoot,
        BAD_CAST
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
        BAD_CAST "r");
    xmlSetNs(slideRoot, nsSldP);

    xmlNodePtr cSldNode =
        xmlNewChild(slideRoot, nsSldP, BAD_CAST "cSld", nullptr);
    xmlNodePtr spTreeNode =
        xmlNewChild(cSldNode, nsSldP, BAD_CAST "spTree", nullptr);

    xmlNodePtr nvGrpSpPrNode =
        xmlNewChild(spTreeNode, nsSldP, BAD_CAST "nvGrpSpPr", nullptr);
    xmlNodePtr cNvPrNode =
        xmlNewChild(nvGrpSpPrNode, nsSldP, BAD_CAST "cNvPr", nullptr);
    xmlNewProp(cNvPrNode, BAD_CAST "id", BAD_CAST "1");
    xmlNewProp(cNvPrNode, BAD_CAST "name", BAD_CAST "");
    xmlNewChild(nvGrpSpPrNode, nsSldP, BAD_CAST "cNvGrpSpPr", nullptr);
    xmlNewChild(nvGrpSpPrNode, nsSldP, BAD_CAST "nvPr", nullptr);

    xmlNewChild(spTreeNode, nsSldP, BAD_CAST "grpSpPr", nullptr);

    // Add slide title shape
    xmlNodePtr titleSpNode =
        xmlNewChild(spTreeNode, nsSldP, BAD_CAST "sp", nullptr);
    xmlNodePtr titleNvSpPrNode =
        xmlNewChild(titleSpNode, nsSldP, BAD_CAST "nvSpPr", nullptr);

    xmlNodePtr titleCNvPrNode =
        xmlNewChild(titleNvSpPrNode, nsSldP, BAD_CAST "cNvPr", nullptr);
    xmlNewProp(titleCNvPrNode, BAD_CAST "id", BAD_CAST "2");
    xmlNewProp(titleCNvPrNode, BAD_CAST "name", BAD_CAST "Title 1");

    xmlNodePtr titleCNvSpPrNode =
        xmlNewChild(titleNvSpPrNode, nsSldP, BAD_CAST "cNvSpPr", nullptr);
    xmlNodePtr titleSpLocks =
        xmlNewChild(titleCNvSpPrNode, nsSldA, BAD_CAST "spLocks", nullptr);
    xmlNewProp(titleSpLocks, BAD_CAST "noGrp", BAD_CAST "1");

    xmlNodePtr titleNvPrNode =
        xmlNewChild(titleNvSpPrNode, nsSldP, BAD_CAST "nvPr", nullptr);
    xmlNodePtr titlePh =
        xmlNewChild(titleNvPrNode, nsSldP, BAD_CAST "ph", nullptr);
    xmlNewProp(titlePh, BAD_CAST "type", BAD_CAST "title");

    xmlNodePtr titleSpPrNode =
        xmlNewChild(titleSpNode, nsSldP, BAD_CAST "spPr", nullptr);
    xmlNodePtr titleXfrmNode =
        xmlNewChild(titleSpPrNode, nsSldA, BAD_CAST "xfrm", nullptr);

    xmlNodePtr titleOffNode =
        xmlNewChild(titleXfrmNode, nsSldA, BAD_CAST "off", nullptr);
    xmlNewProp(titleOffNode, BAD_CAST "x", BAD_CAST "500000");
    xmlNewProp(titleOffNode, BAD_CAST "y", BAD_CAST "500000");

    xmlNodePtr titleExtNode =
        xmlNewChild(titleXfrmNode, nsSldA, BAD_CAST "ext", nullptr);
    xmlNewProp(titleExtNode, BAD_CAST "cx", BAD_CAST "8144000");
    xmlNewProp(titleExtNode, BAD_CAST "cy", BAD_CAST "1000000");

    xmlNodePtr titleTxBodyNode =
        xmlNewChild(titleSpNode, nsSldP, BAD_CAST "txBody", nullptr);
    xmlNewChild(titleTxBodyNode, nsSldA, BAD_CAST "bodyPr", nullptr);
    xmlNewChild(titleTxBodyNode, nsSldA, BAD_CAST "lstStyle", nullptr);

    xmlNodePtr titlePNode =
        xmlNewChild(titleTxBodyNode, nsSldA, BAD_CAST "p", nullptr);
    xmlNodePtr titleRNode =
        xmlNewChild(titlePNode, nsSldA, BAD_CAST "r", nullptr);
    xmlNewChild(titleRNode, nsSldA, BAD_CAST "t", BAD_CAST slideTitle.c_str());

    // Add bullets content shape
    if (slidesArray[s].contains("bullets") &&
        slidesArray[s]["bullets"].is_array()) {
      xmlNodePtr bulletsSpNode =
          xmlNewChild(spTreeNode, nsSldP, BAD_CAST "sp", nullptr);
      xmlNodePtr bulletsNvSpPrNode =
          xmlNewChild(bulletsSpNode, nsSldP, BAD_CAST "nvSpPr", nullptr);

      xmlNodePtr bulletsCNvPrNode =
          xmlNewChild(bulletsNvSpPrNode, nsSldP, BAD_CAST "cNvPr", nullptr);
      xmlNewProp(bulletsCNvPrNode, BAD_CAST "id", BAD_CAST "3");
      xmlNewProp(bulletsCNvPrNode, BAD_CAST "name", BAD_CAST "Content 2");

      xmlNodePtr bulletsCNvSpPrNode =
          xmlNewChild(bulletsNvSpPrNode, nsSldP, BAD_CAST "cNvSpPr", nullptr);
      xmlNodePtr bulletsSpLocks =
          xmlNewChild(bulletsCNvSpPrNode, nsSldA, BAD_CAST "spLocks", nullptr);
      xmlNewProp(bulletsSpLocks, BAD_CAST "noGrp", BAD_CAST "1");

      xmlNodePtr bulletsNvPrNode =
          xmlNewChild(bulletsNvSpPrNode, nsSldP, BAD_CAST "nvPr", nullptr);
      xmlNodePtr bulletsPh =
          xmlNewChild(bulletsNvPrNode, nsSldP, BAD_CAST "ph", nullptr);
      xmlNewProp(bulletsPh, BAD_CAST "type", BAD_CAST "body");
      xmlNewProp(bulletsPh, BAD_CAST "idx", BAD_CAST "1");

      xmlNodePtr bulletsSpPrNode =
          xmlNewChild(bulletsSpNode, nsSldP, BAD_CAST "spPr", nullptr);
      xmlNodePtr bulletsXfrmNode =
          xmlNewChild(bulletsSpPrNode, nsSldA, BAD_CAST "xfrm", nullptr);

      xmlNodePtr bulletsOffNode =
          xmlNewChild(bulletsXfrmNode, nsSldA, BAD_CAST "off", nullptr);
      xmlNewProp(bulletsOffNode, BAD_CAST "x", BAD_CAST "500000");
      xmlNewProp(bulletsOffNode, BAD_CAST "y", BAD_CAST "1800000");

      xmlNodePtr bulletsExtNode =
          xmlNewChild(bulletsXfrmNode, nsSldA, BAD_CAST "ext", nullptr);
      xmlNewProp(bulletsExtNode, BAD_CAST "cx", BAD_CAST "8144000");
      xmlNewProp(bulletsExtNode, BAD_CAST "cy", BAD_CAST "4500000");

      xmlNodePtr bulletsTxBodyNode =
          xmlNewChild(bulletsSpNode, nsSldP, BAD_CAST "txBody", nullptr);
      xmlNewChild(bulletsTxBodyNode, nsSldA, BAD_CAST "bodyPr", nullptr);
      xmlNewChild(bulletsTxBodyNode, nsSldA, BAD_CAST "lstStyle", nullptr);

      for (const auto &b : slidesArray[s]["bullets"]) {
        string bulletText = b.get<string>();
        xmlNodePtr pNode =
            xmlNewChild(bulletsTxBodyNode, nsSldA, BAD_CAST "p", nullptr);
        xmlNodePtr pPrNode =
            xmlNewChild(pNode, nsSldA, BAD_CAST "pPr", nullptr);
        xmlNewProp(pPrNode, BAD_CAST "lvl", BAD_CAST "0");

        xmlNodePtr rNode = xmlNewChild(pNode, nsSldA, BAD_CAST "r", nullptr);
        xmlNewChild(rNode, nsSldA, BAD_CAST "t", BAD_CAST bulletText.c_str());
      }
    }

    string slideFile = "ppt/slides/slide" + to_string(s + 1) + ".xml";
    if (!writeXmlDocument(tempDir / slideFile, slideDoc))
      return false;

    // Relationship for individual slide to layout
    string slideRelsXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships "
        "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
        "relationships\">\n"
        "  <Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/slideLayout\" "
        "Target=\"../slideLayouts/slideLayout1.xml\"/>\n"
        "</Relationships>";
    string slideRelsFile =
        "ppt/slides/_rels/slide" + to_string(s + 1) + ".xml.rels";
    if (!writeRawFile(tempDir / slideRelsFile, slideRelsXml))
      return false;
  }

  return true;
}

// -------------------- Office Wizard Tool Main Entry --------------------
json SystemTools::officeWizard(const json &args) {
  string docType = args.value("document_type", "");
  string filePath = args.value("file_path", "");
  json config = args.value("config", json::object());

  if (docType.empty()) {
    return makeTextResult(
        "Error: 'document_type' is required (word, excel, powerpoint)", true);
  }
  if (filePath.empty()) {
    return makeTextResult("Error: 'file_path' is required", true);
  }

  try {
    fs::path tempDir = createTempDir();
    bool success = false;
    vector<string> foldersToZip;

    if (docType == "word") {
      success = generateWord(tempDir, config);
      foldersToZip = {"[Content_Types].xml", "_rels", "word"};
    } else if (docType == "excel") {
      success = generateExcel(tempDir, config);
      foldersToZip = {"[Content_Types].xml", "_rels", "xl"};
    } else if (docType == "powerpoint") {
      success = generatePowerPoint(tempDir, config);
      foldersToZip = {"[Content_Types].xml", "_rels", "ppt"};
    } else {
      try {
        fs::remove_all(tempDir);
      } catch (...) {
      }
      return makeTextResult("Error: Unknown document type '" + docType + "'",
                            true);
    }

    if (!success) {
      try {
        fs::remove_all(tempDir);
      } catch (...) {
      }
      return makeTextResult("Error: Failed to generate document components",
                            true);
    }

    // Package into ZIP and move to target file path
    if (!packageZip(tempDir, filePath, foldersToZip)) {
      return makeTextResult(
          "Error: Failed to package ZIP file (make sure 'zip' is installed)",
          true);
    }

    ostringstream oss;
    oss << "✅ Office document successfully created:\n";
    oss << "Type: " << docType << "\n";
    oss << "Path: " << filePath << "\n";
    oss << "Size: " << fs::file_size(filePath) << " bytes";
    return makeTextResult(oss.str());

  } catch (const exception &e) {
    return makeTextResult("Error in Office Wizard: " + string(e.what()), true);
  }
}

} // namespace MCP
