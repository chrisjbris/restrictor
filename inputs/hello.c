//=============================================================================
// FILE:
//      input_for_hello.c
//
// DESCRIPTION:
//      Sample input file for the HelloWorld pass.
//
// License: MIT
//=============================================================================
int foo(int a) { return a * 2; }

int bar(int *a, int *b, int *c) { (void)c; return (*a + *b * 2); }
int fez(int a, int b, int c) { return (a + b * 2 + c * 3); }

int rolando_watch_war_games(int x, char *y, char *z) { return x;}

int main(int argc, const char **argv) {
  int a = 1;
  int b = 11;
  int c = 111;
  char f = 'f';
  char g = 'g';
  int d = rolando_watch_war_games(5, &f, &g);

  return (foo(a) + bar(&a, &b, 0) + fez(a, b, c));
}
