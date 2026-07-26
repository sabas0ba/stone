int counter;
int bump(int n) { counter = counter + n; return counter; }
int main() {
  counter = 0;
  bump(3);
  bump(4);
  emit(counter);
  emit(10);
  return 0;
}
