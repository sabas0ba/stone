// 同じ名前の static を持つ別の翻訳単位
static int hidden(int v) { return v + 200; }
int fromother(int v) { return hidden(v); }
