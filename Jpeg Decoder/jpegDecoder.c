#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL3/SDL.h>

struct huffmanNode {
	unsigned char data, type, id;
	struct huffmanNode* left, * right;
};

struct quantTable {
	char data[8][8];
};

struct component {
	unsigned char id;
	unsigned char samplingFactors;
	unsigned char quantTable;
	int oldDC;
	unsigned char dcTable;
	unsigned char acTable;
};

struct pixelBlock {
	unsigned char pixelsR[8][8];
	unsigned char pixelsG[8][8];
	unsigned char pixelsB[8][8];
};

struct huffmanNode* copyTree(struct huffmanNode* tree) {
	struct huffmanNode* newTree = (struct huffmanNode*)malloc(sizeof(struct huffmanNode));
	if (!newTree) {
		printf("allocation failed\n");
	}
	newTree->type = tree->type;
	newTree->id = tree->id;
	newTree->data = tree->data;
	if (tree->left) {
		newTree->left = copyTree(tree->left);
	} else {
		newTree->left = NULL;
	}
	if (tree->right) {
		newTree->right = copyTree(tree->right);
	} else {
		newTree->right = NULL;
	}
	return newTree;
}

void insertIntoTree(struct huffmanNode* root, unsigned int code, int length, unsigned char value) {
	struct huffmanNode* node = root;
	for (int i = length - 1; i >= 0; i--) {
		int bit = (code >> i) & 1;
		if (bit == 0) {
			if (!node->left) {
				node->left = malloc(sizeof(struct huffmanNode));
				node->left->data = 0xFF;
				node->left->left = NULL;
				node->left->right = NULL;
				node->left->id = node->id;
				node->left->type = node->type;
			}
			node = node->left;
		}
		else {
			if (!node->right) {
				node->right = malloc(sizeof(struct huffmanNode));
				node->right->data = 0xFF;
				node->right->left = NULL;
				node->right->right = NULL;
				node->right->id = node->id;
				node->right->type = node->type;
			}
			node = node->right;
		}
	}
	node->data = value;
}

struct huffmanNode* createTreeFromLengths(char* lengths, char* elements, unsigned char htInfo) {
	struct huffmanNode* root = (struct huffmanNode*)malloc(sizeof(struct huffmanNode));
	if (!root) {
		printf("allocation failed\n");
	}
	root->type = (htInfo > 0x0F) ? 1 : 0;
	root->id = htInfo & 0x0F;
	root->data = 0xFF;
	root->left = NULL;
	root->right = NULL;
	unsigned int code = 0;
	int k = 0;
	for (int i = 0; i < 16; i++) {
		int length = i + 1;
		for (int j = 0; j < lengths[i]; j++) {
			insertIntoTree(root, code, length, elements[k]);
			code++;
			k++;
		}
		code <<= 1;
	}
	return root;
}

void deleteTree(struct huffmanNode* root) {
	if (root == NULL) {
		return;
	}
	deleteTree(root->left);
	deleteTree(root->right);
	free(root);
}

void printCodes(struct huffmanNode* root, int arr[], int top) {

	if (root->left && root->right) {
		arr[top] = 0;
		printCodes(root->left, arr, top + 1);
		arr[top] = 1;
		printCodes(root->right, arr, top + 1);
	}
	else if (root->left) {
		arr[top] = 0;
		printCodes(root->left, arr, top + 1);
	}
	else if (root->right) {
		arr[top] = 1;
		printCodes(root->right, arr, top + 1);
	}

	if (root->data != 255) {
		printf("%d: ", root->data);
		for (int i = 0; i < top; i++) {
			printf("%d", arr[i]);
		}
		printf("\n");
	}
}

int main(int argc, char* argv[]) {

	const int startOfImage = 0xFFD8;
	const int startOfFrame0 = 0xFFC0;
	const int startOfFrame2 = 0xFFC2;
	const int huffmanTable = 0xFFC4;
	const int quantTable = 0xFFDB;
	const int startOfScan = 0xFFDA;
	const int endOfImage = 0xFFD9;
	const char zigzag[8][8] =
	{ {0, 1, 5, 6, 14, 15, 27, 28},
	{2, 4, 7, 13, 16, 26, 29, 42},
	{3, 8, 12, 17, 25, 30, 41, 43},
	{9, 11, 18, 24, 31, 40, 44, 53},
	{10, 19, 23, 32, 39, 45, 52, 54},
	{20, 22, 33, 38, 46, 51, 55, 60},
	{21, 34, 37, 47, 50, 56, 59, 61},
	{35, 36, 48, 49, 57, 58, 62, 63} };
	int rearranged[8][8];
	double idctTable[8][8];
	char numComponents;
	unsigned char numQTables;
	struct component* components[3];
	int idctPrecision = 8;
	char dcCount = 0;
	char acCount = 0;
	char tableCount = 0;
	char sfyh = 1, sfyv = 1, sfy = 1;
	struct pixelBlock* imgBlocks = NULL, * superBlocks = NULL;
	unsigned char* linearizedImg;
	unsigned short height, width;
	struct huffmanNode* dcTrees[8];
	struct huffmanNode* acTrees[8];
	struct quantTable* qtables[8];
	unsigned char currentBytes[2];
	size_t bytesRead;
	char lengths[16];
	FILE* img_ptr;
	char* fileName = argv[1];

	errno_t fileError = fopen_s(&img_ptr, fileName, "rb");
	if (fileError != 0) {
		perror("Error opening file");
		return 1;
	}
	while ((bytesRead = fread(currentBytes, 1, 1, img_ptr)) > 0) {
		if (bytesRead == 1) {
			unsigned short value = (unsigned char)currentBytes[1] << 8 | ((unsigned char)currentBytes[0]);
			if (value == huffmanTable) {
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				unsigned short length = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				unsigned short track = 0;
				while (track < length - 2) {
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
					track++;
					char htInfo = currentBytes[0];
					int numElements = 0;
					bytesRead = fread(lengths, 1, 16, img_ptr);
					track += 16;
					for (int i = 0; i < 16; i++) {
						numElements += lengths[i];
					}
					char* elements = malloc(numElements);
					bytesRead = fread(elements, 1, numElements, img_ptr);
					track += numElements;
					struct huffmanNode* tree = createTreeFromLengths(lengths, elements, htInfo);
					int arr[16];
					printCodes(tree, arr, 0);
					if (tree->type == 0) {
						dcTrees[dcCount] = tree;
						dcCount++;
					}
					else {
						acTrees[acCount] = tree;
						acCount++;
					}
					free(elements);
					printf("\n");
				}
			}
			else if (value == quantTable) {
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				unsigned short length = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				numQTables = length / 64;
				printf("there are %d quant tables\n", numQTables);
				for (int i = 0; i < numQTables; i++) {
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
					char qtinfo = currentBytes[0];
					struct quantTable* qt = (struct quantTable*)malloc(sizeof(struct quantTable));
					if (!qt) {
						printf("allocation failed\n");
					}
					for (int j = 0; j < 8; j++) {
						for (int k = 0; k < 8; k++) {
							bytesRead = fread(currentBytes, 1, 1, img_ptr);
							qt->data[j][k] = currentBytes[0];
						}
					}
					qtables[tableCount++] = qt;
				}
			}
			else if (value == startOfFrame0) {
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				unsigned short length = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				char precision = currentBytes[0];
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				height = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				while (height % 8 != 0) {
					height++;
				}
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				width = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				while (width % 8 != 0) {
					width++;
				}
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				numComponents = currentBytes[0];
				for (int i = 0; i < numComponents; i++) {
					struct component* newComponent = (struct component*)malloc(sizeof(struct component));
					if (!newComponent) {
						printf("allocation failed\n");
					}
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
					newComponent->id = currentBytes[0];
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
					newComponent->samplingFactors = currentBytes[0];
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
					newComponent->quantTable = currentBytes[0];
					newComponent->oldDC = 0;
					components[i] = newComponent;
				}
			}
			else if (value == startOfScan) {
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				unsigned short length = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				char numComponentsScan = currentBytes[0];
				for (int g = 0; g < numComponentsScan; g++) {
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
					char componentId = currentBytes[0];
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
					char tableNums = currentBytes[0];
					components[componentId - 1]->dcTable = (tableNums >> 4) & 0x0F;
					components[componentId - 1]->acTable = tableNums & 0x0F;
				}
				sfyh = components[0]->samplingFactors >> 4 & 0x0F;
				sfyv = components[0]->samplingFactors & 0x0F;
				sfy = sfyh * sfyv;
				char sfcbh = components[1]->samplingFactors >> 4 & 0x0F;
				char sfcbv = components[1]->samplingFactors & 0x0F;
				char sfcrh = components[2]->samplingFactors >> 4 & 0x0F;
				char sfcrv = components[2]->samplingFactors & 0x0F;
				int cbRatioH = sfyh / sfcbh;
				int cbRatioV = sfyv / sfcbv;
				int crRatioH = sfyh / sfcrh;
				int crRatioV = sfyv / sfcrv;
				for (int i = 0; i < 3; i++) {
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
				}
				for (int u = 0; u < idctPrecision; u++) {
					for (int v = 0; v < idctPrecision; v++) {
						idctTable[u][v] = cos(((2.0 * v + 1.0) * u * 3.14159) / 16.0);
					}
				}
				char endFlag = 0;
				char endImgFlag = 0;
				int count2 = 0;
				imgBlocks = malloc(sizeof(struct pixelBlock) * (height / 8) * (width / 8));
				printf("imgBlocks size: %d bytes, %d blocks\n", sizeof(struct pixelBlock) * (height / 8) * (width / 8), (height / 8) * (width / 8));
				if (!imgBlocks) {
					printf("allocation failed\n");
				}
				unsigned int counter = 0;
				char offset = 0;
				double out[8][8][8];
				for (int i = 0; i < 3; i++) {
					for (int j = 0; j < 8; j++) {
						for (int k = 0; k < 8; k++) {
							out[i][j][k] = 0.0;
						}
					}
				}
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				currentBytes[1] = currentBytes[0];
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				if (bytesRead != 1) {
					if (feof(img_ptr)) {
						endFlag = 1;
					}
					break;
				}
				for (int y = 0; y < height / (8 * sfyv); y++) {
					for (int x = 0; x < width / (8 * sfyh); x++) {
						if (!endImgFlag) {
							int blockNum = 0;
							for (int i = 0; i < numComponents; i++) {
								if (!endFlag) {
									int idctBase[8][8];
									struct component* currentComponent = components[i];
									int qtnum = currentComponent->quantTable;
									char sampleFactorH = currentComponent->samplingFactors >> 4 & 0x0F;
									char sampleFactorV = currentComponent->samplingFactors & 0x0F;
									for (int horiz = 0; horiz < sampleFactorH; horiz++) {
										for (int vert = 0; vert < sampleFactorV; vert++) {
											struct quantTable* qt = qtables[qtnum];
											struct huffmanNode* dcTree = dcTrees[currentComponent->dcTable];
											struct huffmanNode* node = copyTree(dcTree);
											struct huffmanNode* root = node;
											while (node->data == 0xFF) {
												if (((currentBytes[1] >> (7 - offset)) & 1) != 0 && (node->right)) {
													node = node->right;
												}
												else if (node->left) {
													node = node->left;
												}
												else {
													idctBase[0][0] = 0;
												}
												offset++;
												if (offset == 8) {
													offset = 0;
													currentBytes[1] = currentBytes[0];
													bytesRead = fread(currentBytes, 1, 1, img_ptr);
													if (currentBytes[1] == 0xFF) {
														if (currentBytes[0] == 0) {
															printf("Skipping FF00 at %x\n", ftell(img_ptr));
															bytesRead = fread(currentBytes, 1, 1, img_ptr);
															if (bytesRead != 1) {
																if (feof(img_ptr)) {
																	endFlag = 1;
																}
																break;
															}
														}
														else {
															break;
														}
													}
												}
											}
											char category = node->data & 0x0F;
											int magnitude = 0;
											for (int t = 0; t < category; t++) {
												if (offset == 8) {
													offset = 0;
													currentBytes[1] = currentBytes[0];
													bytesRead = fread(currentBytes, 1, 1, img_ptr);
													if (currentBytes[1] == 0xFF) {
														if (currentBytes[0] == 0) {
															printf("Skipping FF00 at %x\n", ftell(img_ptr));
															bytesRead = fread(currentBytes, 1, 1, img_ptr);
															if (bytesRead != 1) {
																if (feof(img_ptr)) {
																	endFlag = 1;
																}
																break;
															}
														}
														else {
															printf("Marker found: %x\n", currentBytes[1] << 8 | currentBytes[0]);
															if (currentBytes[0] == 0xD9) {
																endImgFlag = 1;
																break;
															}
															break;
														}
													}
												}
												magnitude = (magnitude << 1) | ((currentBytes[1] >> (7 - offset)) & 1);
												offset++;
												if (offset == 8) {
													offset = 0;
													currentBytes[1] = currentBytes[0];
													bytesRead = fread(currentBytes, 1, 1, img_ptr);
													if (currentBytes[1] == 0xFF) {
														if (currentBytes[0] == 0) {
															printf("Skipping FF00 at %x\n", ftell(img_ptr));
															bytesRead = fread(currentBytes, 1, 1, img_ptr);
															if (bytesRead != 1) {
																if (feof(img_ptr)) {
																	endFlag = 1;
																}
																break;
															}
														}
														else {
															printf("Marker found: %x\n", currentBytes[1] << 8 | currentBytes[0]);
															if (currentBytes[0] == 0xD9) {
																endImgFlag = 1;
																break;
															}
															break;
														}
													}
												}
											}
											if (magnitude < (1 << (category - 1))) {
												magnitude -= (1 << category) - 1;
											}
											int newdc = currentComponent->oldDC + magnitude;
											idctBase[0][0] = newdc;
											currentComponent->oldDC = newdc;
											struct huffmanNode* acTree = acTrees[currentComponent->acTable];
											for (int j = 1; j < 64; j++) {
												struct huffmanNode* node = copyTree(acTree);
												while (node->data == 0xFF) {
													if (((currentBytes[1] >> (7 - offset)) & 1) != 0 && (node->right)) {
														node = node->right;
													}
													else if (((currentBytes[1] >> (7 - offset)) & 1) == 0 && (node->left)) {
														node = node->left;
													}
													else {
														while (j < 64) {
															idctBase[j / 8][j % 8] = 0;
															j++;
														}
														break;
													}
													offset++;
													if (offset == 8) {
														offset = 0;
														currentBytes[1] = currentBytes[0];
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (currentBytes[1] == 0xFF) {
															if (currentBytes[0] == 0) {
																printf("Skipping FF00 at %x\n", ftell(img_ptr));
																bytesRead = fread(currentBytes, 1, 1, img_ptr);
																if (bytesRead != 1) {
																	if (feof(img_ptr)) {
																		endFlag = 1;
																	}
																	break;
																}
															}
															else {
																printf("Marker found: %x\n", currentBytes[1] << 8 | currentBytes[0]);
																if (currentBytes[0] == 0xD9) {
																	endImgFlag = 1;
																	break;
																}
																break;
															}
														}
													}
												}
												if (node->data == 0) {
													while (j < 64) {
														idctBase[j / 8][j % 8] = 0;
														j++;
													}
													break;
												}
												char runLength = (node->data >> 4) & 0x0F;
												for (int q = 0; q < runLength; q++) {
													if (j >= 64) {
														break;
													}
													idctBase[j / 8][j % 8] = 0;
													j++;
												}
												if (node->data == 0xF0) {
													if (j >= 64) {
														break;
													}
													idctBase[j / 8][j % 8] = 0;
													j++;
												}
												char category = node->data & 0x0F;
												if (category == 0 && runLength != 0x0F) {
													while (j < 64) {
														idctBase[j / 8][j % 8] = 0;
														j++;
													}
													printf("end of block\n");
													break;
												}
												int magnitude = 0;
												for (int t = 0; t < category; t++) {
													if (offset == 8) {
														offset = 0;
														currentBytes[1] = currentBytes[0];
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (currentBytes[1] == 0xFF) {
															if (currentBytes[0] == 0) {
																printf("Skipping FF00 at %x\n", ftell(img_ptr));
																bytesRead = fread(currentBytes, 1, 1, img_ptr);
																if (bytesRead != 1) {
																	if (feof(img_ptr)) {
																		endFlag = 1;
																	}
																	break;
																}
															}
															else {
																printf("Marker found: %x\n", currentBytes[1] << 8 | currentBytes[0]);
																if (currentBytes[0] == 0xD9) {
																	endImgFlag = 1;
																	break;
																}
																break;
															}
														}
													}
													magnitude = (magnitude << 1) | ((currentBytes[1] >> (7 - offset)) & 1);
													offset++;
													if (offset == 8) {
														offset = 0;
														currentBytes[1] = currentBytes[0];
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (currentBytes[1] == 0xFF) {
															if (currentBytes[0] == 0) {
																printf("Skipping FF00 at %x\n", ftell(img_ptr));
																bytesRead = fread(currentBytes, 1, 1, img_ptr);
																if (bytesRead != 1) {
																	if (feof(img_ptr)) {
																		endFlag = 1;
																	}
																	break;
																}
															}
															else {
																printf("Marker found: %x\n", currentBytes[1] << 8 | currentBytes[0]);
																if (currentBytes[0] == 0xD9) {
																	endImgFlag = 1;
																	break;
																}
																break;
															}
														}
													}
												}
												value = magnitude;
												if (magnitude < (1 << (category - 1))) {
													magnitude -= (1 << category) - 1;
												}
												idctBase[j / 8][j % 8] = magnitude;
											}
											for (int a = 0; a < 8; a++) {
												for (int b = 0; b < 8; b++) {
													idctBase[a][b] = idctBase[a][b] * qt->data[a][b];
												}
											}
											for (int a = 0; a < 8; a++) {
												for (int b = 0; b < 8; b++) {
													rearranged[a][b] = idctBase[zigzag[a][b] / 8][zigzag[a][b] % 8];
												}
											}
											for (int a = 0; a < 8; a++) {
												for (int b = 0; b < 8; b++) {
													double localSum = 0.0;
													for (int u = 0; u < idctPrecision; u++) {
														for (int v = 0; v < idctPrecision; v++) {
															double normCoeff = 1.0;
															if (u == 0) {
																normCoeff *= 1.0 / sqrt(2.0);
															}
															if (v == 0) {
																normCoeff *= 1.0 / sqrt(2.0);
															}
															localSum += normCoeff * (double)rearranged[u][v] * idctTable[u][a] * idctTable[v][b];
														}
													}
													out[blockNum][a][b] = round(localSum / 4.0) + 128;
												}
											}
											blockNum++;
											deleteTree(root);
										}
									}
								} else {
									printf("end of file found\n");
									break;
								}
							}
							if (numComponents == 3) {
								for (int z = 0; z < sfy; z++) {
									for (int yIndex = 0; yIndex < 64; yIndex++) {
										int yPosX = yIndex % 8;
										int yPosY = yIndex / 8;
										int cbPosX = ((z / sfyh) * 8 + yPosX) / cbRatioH;
										int cbPosY = ((z % sfyh) * 8 + yPosY) / cbRatioV;
										int crPosX = ((z / sfyh) * 8 + yPosX) / crRatioH;
										int crPosY = ((z % sfyh) * 8 + yPosY) / crRatioV;
										int Y = out[z][yPosX][yPosY];
										int Cb = out[sfy][cbPosX][cbPosY];
										int Cr = out[sfy + 1][crPosX][crPosY];
										float R = Y + 1.402 * (Cr - 128);
										float G = Y - 0.344136 * (Cb - 128) - 0.714136 * (Cr - 128);
										float B = Y + 1.772 * (Cb - 128);
										imgBlocks[count2].pixelsR[yPosX][yPosY] = (unsigned char)(R < 0 ? 0 : R > 255 ? 255 : round(R));
										imgBlocks[count2].pixelsG[yPosX][yPosY] = (unsigned char)(G < 0 ? 0 : G > 255 ? 255 : round(G));
										imgBlocks[count2].pixelsB[yPosX][yPosY] = (unsigned char)(B < 0 ? 0 : B > 255 ? 255 : round(B));
									}
									count2++;
								}
							}
							printf("completed block %d, address %x\n", count2, ftell(img_ptr));
							fflush(stdout);
						}
						else {
							printf("end of image found\n");
							break;
						}
					}
				}
			}
			else if (value == startOfFrame2) {
				printf("Progressive JPEGs not currently supported\n");
				getchar();
				return 0;
			}
			else {
				currentBytes[1] = currentBytes[0];
			}
		}
	}
	fflush(stdout);
	for (int i = 0; i < numQTables; i++) {
		free(qtables[i]);
	}
	for (int i = 0; i < numComponents; i++) {
		free(components[i]);
	}
	int block = 0;
	superBlocks = malloc(sizeof(struct pixelBlock) * (height / 8) * (width / 8));
	for (int y = 0; y < height / (8 * sfyv); y++) {
		for (int x = 0; x < width / (8 * sfyh); x++) {
			for (int subY = 0; subY < sfyv; subY++) {
				for (int subX = 0; subX < sfyh; subX++) {
					int destX = x * sfyh + subX;
					int destY = y * sfyv + subY;
					int dest = destY * width / 8 + destX;
					superBlocks[dest] = imgBlocks[block++];
				}
			}
		}
	}
	free(imgBlocks);
	linearizedImg = malloc(3 * height * width);
	printf("image size: %d\n", 3 * height * width);
	if (!linearizedImg) {
		printf("allocation failed\n");
	}
	int count = 0;
	for (int y = 0; y < height / 8; y++) {
		for (int y2 = 0; y2 < 8; y2++) {
			for (int x = 0; x < width / 8; x++) {
				int blockPos = (width / 8) * y + x;
				for (int x2 = 0; x2 < 8; x2++) {
					unsigned char rValue = superBlocks[blockPos].pixelsR[y2][x2];
					unsigned char gValue = superBlocks[blockPos].pixelsG[y2][x2];
					unsigned char bValue = superBlocks[blockPos].pixelsB[y2][x2];
					linearizedImg[count++] = rValue;
					linearizedImg[count++] = gValue;
					linearizedImg[count++] = bValue;
				}
			}
		}
	}
	free(superBlocks);
	int errorCode = SDL_Init(SDL_INIT_VIDEO);
	if (errorCode != 0) {
		SDL_Window* window = SDL_CreateWindow(fileName, width, height, 0);
		SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
		SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, width, height);
		SDL_UpdateTexture(texture, NULL, linearizedImg, 3 * width);
		SDL_RenderClear(renderer);
		SDL_RenderTexture(renderer, texture, NULL, NULL);
		SDL_RenderPresent(renderer);
		getchar();
		SDL_DestroyWindow(window);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyTexture(texture);
		SDL_Quit();
	}
	else {
		printf("SDL failed to initialize\n");
		printf("%s\n", SDL_GetError());
	}
	free(linearizedImg);
	fclose(img_ptr);
	return 0;
}
