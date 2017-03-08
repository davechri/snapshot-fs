/*
 * Main.H
 *
 *  Created on: Oct 7, 2011
 *      Author: christen
 */

#ifndef MAIN_H_
#define MAIN_H_

class Main {
public:
	/**
	 * @brief Main function.
	 * @param argc	Argument count.
	 * @param argv	Argument list.
	 */
	static int main(int argc, char *argv[]);

private:
	Main(); // Disallow constructor
	static int verifyConfig();
	static int verifyNetwork();
	static void usage();
	static int tool(int argc, char *argv[]);
	static int parseHostAndDir(char *iArg);
	static int passwordPrompt();
	static void mkCacheDir(const char *iPath);
};

#endif /* MAIN_H_ */
