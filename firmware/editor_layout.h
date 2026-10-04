#pragma once
#include <string>
#include <vector>
#include <algorithm>
namespace snowball {
struct TextRow {size_t begin,end;};
struct EditorLayout {std::vector<TextRow> rows;size_t cursorRow=0;};
// UTF-8 byte spans retain the real cursor location. Width comes from the actual
// display font; the host tests pass a deterministic width for boundary cases.
template<class Width> EditorLayout editorLayout(const std::string& text,size_t caret,int maximum,Width width){
  EditorLayout result;size_t start=0,p=0;int used=0;
  while(p<text.size()){
    size_t end=p+1;while(end<text.size()&&((unsigned char)text[end]&0xc0)==0x80)++end;
    const auto glyph=text.substr(p,end-p);
    if(glyph=="\n"){result.rows.push_back({start,p});start=end;used=0;}
    else {int pixels=width(glyph);if(used+pixels>maximum&&p>start){result.rows.push_back({start,p});start=p;used=0;}used+=pixels;}
    p=end;
  }
  result.rows.push_back({start,text.size()});
  for(size_t row=0;row<result.rows.size();++row)if(caret>=result.rows[row].begin)result.cursorRow=row;
  return result;
}
}
