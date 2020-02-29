#include "ProxyServer.h"

int main()
{
	system("mkdir cache\\");
	cout << "HTTP Proxy Server" << endl;
	runServer();

	return 0;
}