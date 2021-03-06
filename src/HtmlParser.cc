/*
 * nghttp2 - HTTP/2.0 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "HtmlParser.h"

#include <libxml/uri.h>

#include "util.h"

namespace nghttp2 {

ParserData::ParserData(const std::string& base_uri)
  : base_uri(base_uri)
{}

HtmlParser::HtmlParser(const std::string& base_uri)
  : base_uri_(base_uri),
    parser_ctx_(nullptr),
    parser_data_(base_uri)
{}

HtmlParser::~HtmlParser()
{
  htmlFreeParserCtxt(parser_ctx_);
}

namespace {
const char* get_attr(const xmlChar **attrs, const char *name)
{
  if(attrs == nullptr) {
    return nullptr;
  }
  for(; *attrs; attrs += 2) {
    if(util::strieq(reinterpret_cast<const char*>(attrs[0]), name)) {
      return reinterpret_cast<const char*>(attrs[1]);
    }
  }
  return nullptr;
}
} // namespace

namespace {
void add_link(ParserData *parser_data, const char *uri, RequestPriority pri)
{
  auto u = xmlBuildURI(reinterpret_cast<const xmlChar*>(uri),
                       reinterpret_cast<const xmlChar*>
                       (parser_data->base_uri.c_str()));
  if(u) {
    parser_data->links.push_back(std::make_pair(reinterpret_cast<char*>(u),
                                                pri));
    free(u);
  }
}
} // namespace

namespace {
void start_element_func
(void* user_data,
 const xmlChar *name,
 const xmlChar **attrs)
{
  auto parser_data = reinterpret_cast<ParserData*>(user_data);
  if(util::strieq(reinterpret_cast<const char*>(name), "link")) {
    auto rel_attr = get_attr(attrs, "rel");
    auto href_attr = get_attr(attrs, "href");
    if(!href_attr) {
      return;
    }
    if(util::strieq(rel_attr, "shortcut icon")) {
      add_link(parser_data, href_attr, REQ_PRI_LOWEST);
    } else if(util::strieq(rel_attr, "stylesheet")) {
      add_link(parser_data, href_attr, REQ_PRI_MEDIUM);
    }
  } else if(util::strieq(reinterpret_cast<const char*>(name), "img")) {
    auto src_attr = get_attr(attrs, "src");
    if(!src_attr) {
      return;
    }
    add_link(parser_data, src_attr, REQ_PRI_LOWEST);
  } else if(util::strieq(reinterpret_cast<const char*>(name), "script")) {
    auto src_attr = get_attr(attrs, "src");
    if(!src_attr) {
      return;
    }
    add_link(parser_data, src_attr, REQ_PRI_MEDIUM);
  }
}
} // namespace

namespace {
xmlSAXHandler saxHandler =
  {
    nullptr, // internalSubsetSAXFunc
    nullptr, // isStandaloneSAXFunc
    nullptr, // hasInternalSubsetSAXFunc
    nullptr, // hasExternalSubsetSAXFunc
    nullptr, // resolveEntitySAXFunc
    nullptr, // getEntitySAXFunc
    nullptr, // entityDeclSAXFunc
    nullptr, // notationDeclSAXFunc
    nullptr, // attributeDeclSAXFunc
    nullptr, // elementDeclSAXFunc
    nullptr, // unparsedEntityDeclSAXFunc
    nullptr, // setDocumentLocatorSAXFunc
    nullptr, // startDocumentSAXFunc
    nullptr, // endDocumentSAXFunc
    &start_element_func, // startElementSAXFunc
    nullptr, // endElementSAXFunc
    nullptr, // referenceSAXFunc
    nullptr, // charactersSAXFunc
    nullptr, // ignorableWhitespaceSAXFunc
    nullptr, // processingInstructionSAXFunc
    nullptr, // commentSAXFunc
    nullptr, // warningSAXFunc
    nullptr, // errorSAXFunc
    nullptr, // fatalErrorSAXFunc
    nullptr, // getParameterEntitySAXFunc
    nullptr, // cdataBlockSAXFunc
    nullptr, // externalSubsetSAXFunc
    0,       // unsigned int initialized
    nullptr, // void * _private
    nullptr, // startElementNsSAX2Func
    nullptr, // endElementNsSAX2Func
    nullptr, // xmlStructuredErrorFunc
  };
} // namespace

int HtmlParser::parse_chunk(const char *chunk, size_t size, int fin)
{
  if(!parser_ctx_) {
    parser_ctx_ = htmlCreatePushParserCtxt(&saxHandler,
                                           &parser_data_,
                                           chunk, size,
                                           base_uri_.c_str(),
                                           XML_CHAR_ENCODING_NONE);
    if(!parser_ctx_) {
      return -1;
    } else {
      if(fin) {
        return parse_chunk_internal(nullptr, 0, fin);
      } else {
        return 0;
      }
    }
  } else {
    return parse_chunk_internal(chunk, size, fin);
  }
}

int HtmlParser::parse_chunk_internal(const char *chunk, size_t size,
                                     int fin)
{
  int rv = htmlParseChunk(parser_ctx_, chunk, size, fin);
  if(rv == 0) {
    return 0;
  } else {
    return -1;
  }
}

const std::vector<std::pair<std::string, RequestPriority>>&
HtmlParser::get_links() const
{
  return parser_data_.links;
}

void HtmlParser::clear_links()
{
  parser_data_.links.clear();
}

} // namespace nghttp2
