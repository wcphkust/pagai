
int f(int x) {
	return x+2;
}

int main(int argc, char ** argv) {
	int x;
	int y;
	int i;

	while (x < 100000 && x > 0) {
	x = 2;
	y = x + 50;

	for (i=0; i < y; i++) {
		x = x + 2;
		x = f(x);
		x = x - 2;
	}

	x = x+1;

	for (i=0; i < y; i++) {
		x = x + 2;
	}

	}
	return x;
}
