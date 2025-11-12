#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL3/SDL.h>

struct huffmanNode {
	unsigned char data, type, id;
	struct huffmanNode* left, * right;
};

struct quantTable {
	unsigned char data[8][8];
	unsigned char info;
};

struct component {
	unsigned char id;
	unsigned char samplingFactors;
	unsigned char quantTable;
	int oldDC;
	unsigned char dcTable;
	unsigned char acTable;
};

struct componentBlock {
	float pixels[8][8];
	unsigned char componentId;
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
		} else {
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
	} else if (root->left) {
		arr[top] = 0;
		printCodes(root->left, arr, top + 1);
	} else if (root->right) {
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
	double idctTable[8][8];
	char numComponents;
	unsigned char numQTables;
	struct component* components[3];
	int idctPrecision;
	char tableCount = 0;
	char sfyh = 1, sfyv = 1, sfy = 1;
	int qBlockNum = 0;
	int eobrun = 0;
	int totalBlocks;
	struct pixelBlock* imgBlocks = NULL;
	struct componentBlock* preTransBlocks = NULL;
	struct componentBlock* out = NULL;
	unsigned char* linearizedImg;
	unsigned short height, trueHeight, width, trueWidth;
	int yBlocksPerRow, yBlocksPerCol, totalYBlocks;
	int cbBlocksPerRow, cbBlocksPerCol, totalCbBlocks;
	int crBlocksPerRow, crBlocksPerCol, totalCrBlocks;
	struct huffmanNode* dcTrees[8];
	struct huffmanNode* acTrees[8];
	struct quantTable* qtables[8];
	unsigned char currentBytes[2];
	size_t bytesRead;
	char lengths[16];
	FILE* img_ptr;
	char* fileName = argv[1];
	struct componentBlock* qBlocks = NULL;
	struct component* currentComponent = NULL;
	char sfcbh;
	char sfcbv;
	char sfcrh;
	char sfcrv;
	char cbRatioH;
	char cbRatioV;
	char crRatioH;
	char crRatioV;
	bool progressive;
	SDL_Window* window = NULL;
	SDL_Renderer* renderer = NULL;
	SDL_Texture* texture = NULL;

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
						dcTrees[tree->id] = tree;
					} else {
						acTrees[tree->id] = tree;
					}
					free(elements);
					printf("\n");
				}
			} else if (value == quantTable) {
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
					qt->info = qtinfo;
					for (int j = 0; j < 8; j++) {
						for (int k = 0; k < 8; k++) {
							bytesRead = fread(currentBytes, 1, 1, img_ptr);
							qt->data[j][k] = currentBytes[0];
						}
					}
					qtables[tableCount++] = qt;
				}
			} else if (value == startOfFrame0 || value == startOfFrame2) {
				progressive = false;
				if (value == startOfFrame2) {
					progressive = true;
				}
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				unsigned short length = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				idctPrecision = currentBytes[0];
				for (int u = 0; u < idctPrecision; u++) {
					for (int v = 0; v < idctPrecision; v++) {
						idctTable[u][v] = cos(((2.0 * v + 1.0) * u * 3.14159) / 16.0);
					}
				}
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				trueHeight = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				height = trueHeight;
				while (height % 8 != 0) {
					height++;
				}
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				trueWidth = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				width = trueWidth;
				while (width % 8 != 0) {
					width++;
				}
				yBlocksPerRow = width / 8;
				yBlocksPerCol = height / 8;
				totalYBlocks = yBlocksPerRow * yBlocksPerCol;
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
				sfyh = components[0]->samplingFactors >> 4 & 0x0F;
				sfyv = components[0]->samplingFactors & 0x0F;
				sfy = sfyh * sfyv;
				sfcbh = components[1]->samplingFactors >> 4 & 0x0F;
				sfcbv = components[1]->samplingFactors & 0x0F;
				sfcrh = components[2]->samplingFactors >> 4 & 0x0F;
				sfcrv = components[2]->samplingFactors & 0x0F;
				cbRatioH = sfyh / sfcbh;
				cbRatioV = sfyv / sfcbv;
				crRatioH = sfyh / sfcrh;
				crRatioV = sfyv / sfcrv;
				cbBlocksPerRow = ceil((float)yBlocksPerRow / cbRatioH);
				cbBlocksPerCol = ceil((float)yBlocksPerCol / cbRatioV);
				totalCbBlocks = cbBlocksPerCol * cbBlocksPerRow;
				crBlocksPerRow = ceil((float)yBlocksPerRow / crRatioH);
				crBlocksPerCol = ceil((float)yBlocksPerCol / crRatioV);
				totalCrBlocks = crBlocksPerCol * crBlocksPerRow;
				totalBlocks = totalYBlocks + totalCbBlocks + totalCrBlocks;
				qBlocks = malloc(sizeof(struct componentBlock) * totalBlocks);
				for (int i = 0; i < (totalYBlocks + totalCbBlocks + totalCrBlocks); i++) {
					for (int j = 0; j < 8; j++) {
						for (int k = 0; k < 8; k++) {
							qBlocks[i].pixels[j][k] = 0;
						}
					}
					if (i < totalYBlocks) {
						qBlocks[i].componentId = 1;
					} else if (i < totalYBlocks + totalCbBlocks) {
						qBlocks[i].componentId = 2;
					} else if (i < totalBlocks) {
						qBlocks[i].componentId = 3;
					} else {
						qBlocks[i].componentId = 0;
					}
				}
				out = malloc(sizeof(struct componentBlock) * totalBlocks);
				for (int i = 0; i < totalBlocks; i++) {
					for (int j = 0; j < 8; j++) {
						for (int k = 0; k < 8; k++) {
							out[i].pixels[j][k] = 0;
						}
					}
				}
				preTransBlocks = malloc(sizeof(struct componentBlock) * totalBlocks);
				for (int i = 0; i < totalBlocks; i++) {
					for (int j = 0; j < 8; j++) {
						for (int k = 0; k < 8; k++) {
							preTransBlocks[i].pixels[j][k] = 0;
						}
					}
				}
				currentComponent = components[0];
				int errorCode = SDL_Init(SDL_INIT_VIDEO);
				if (errorCode != 0) {
					window = SDL_CreateWindow(fileName, width, height, 0);
					renderer = SDL_CreateRenderer(window, NULL);
					texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, width, height);
				} else {
					printf("SDL failed to initialize\n");
					printf("%s\n", SDL_GetError());
				}
			} else if (value == startOfScan) {
				printf("Scan started at %x\n", ftell(img_ptr));
				bytesRead = fread(currentBytes, 1, 2, img_ptr);
				unsigned short length = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				char numComponentsScan = currentBytes[0];
				char componentId = 0;
				for (int g = 0; g < numComponentsScan; g++) {
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
					componentId = currentBytes[0];
					bytesRead = fread(currentBytes, 1, 1, img_ptr);
					char tableNums = currentBytes[0];
					components[componentId - 1]->dcTable = (tableNums >> 4) & 0x0F;
					components[componentId - 1]->acTable = tableNums & 0x0F;
				}
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				char ss = currentBytes[0];
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				char se = currentBytes[0];
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				char ah = currentBytes[0] >> 4 & 0x0F;
				char al = currentBytes[0] & 0x0F;
				printf("ss: %d se: %d ah: %d al: %d, numComponentsScan: %d, componentId: %d\n", ss, se, ah, al, numComponentsScan, componentId);
				char endFlag = 0;
				char endImgFlag = 0;
				char endScanFlag = 0;
				imgBlocks = malloc(sizeof(struct pixelBlock) * totalYBlocks);
				if (!imgBlocks) {
					printf("allocation failed\n");
				}
				int blocks = 0;
				char offset = 0;;
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				currentBytes[1] = currentBytes[0];
				bytesRead = fread(currentBytes, 1, 1, img_ptr);
				if (bytesRead != 1) {
					if (feof(img_ptr)) {
						endFlag = 1;
					}
					break;
				}
				if (currentBytes[1] == 0xFF) {
					if (currentBytes[0] == 0) {
						bytesRead = fread(currentBytes, 1, 1, img_ptr);
						if (bytesRead != 1) {
							if (feof(img_ptr)) {
								endFlag = 1;
							}
							break;
						}
					} else {
						printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
						if (currentBytes[0] == 0xD9) {
							endImgFlag = 1;
						}
						endScanFlag = 1;
						endFlag = 1;
						break;
					}
				}
				if (componentId == 1 || numComponentsScan == numComponents) {
					qBlockNum = 0;
				} else if (componentId == 2) {
					qBlockNum = totalYBlocks;
				} else if (componentId == 3) {
					qBlockNum = totalYBlocks + totalCbBlocks;
				}
				if (numComponentsScan == 1) {
					if (componentId == 1) {
						blocks = totalYBlocks;
					} else if (componentId == 2) {
						blocks = totalCbBlocks;
					} else if (componentId == 3) {
						blocks = totalCrBlocks;
					}
				} else if (numComponentsScan == 3) {
					blocks = totalBlocks;
					componentId = 1;
				}
				currentComponent = components[componentId - 1];
				for (int x = 0; x < blocks; x++) {
					if (!endImgFlag && !endScanFlag) {
						if (!endFlag) {
							int idctBase[8][8];
							int qtnum = currentComponent->quantTable;
							struct quantTable* qt = qtables[qtnum];
							char sampleFactorH = currentComponent->samplingFactors >> 4 & 0x0F;
							char sampleFactorV = currentComponent->samplingFactors & 0x0F;
							for (int a = 0; a < 8; a++) {
								for (int b = 0; b < 8; b++) {
									idctBase[a][b] = 0;
								}
							}
							preTransBlocks[qBlockNum].componentId = currentComponent->id;
							if (eobrun == 0) {
								if (ss == 0) {
									if (ah == 0) {
										struct huffmanNode* dcTree = dcTrees[currentComponent->dcTable];
										struct huffmanNode* node = copyTree(dcTree);
										while (node->data == 0xFF) {
											if (((currentBytes[1] >> (7 - offset)) & 1) != 0 && (node->right)) {
												node = node->right;
											} else if (node->left) {
												node = node->left;
											} else {
												idctBase[0][0] = 0;
											}
											offset++;
											if (offset == 8) {
												offset = 0;
												currentBytes[1] = currentBytes[0];
												bytesRead = fread(currentBytes, 1, 1, img_ptr);
												if (currentBytes[1] == 0xFF) {
													if (currentBytes[0] == 0) {
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (bytesRead != 1) {
															if (feof(img_ptr)) {
																endFlag = 1;
															}
															break;
														}
													} else {
														printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
														if (currentBytes[0] == 0xD9) {
															endImgFlag = 1;
														}
														endScanFlag = 1;
														endFlag = 1;
														break;
													}
												}
											}
										}
										char category = node->data & 0x0F;
										int magnitude = 0;
										for (int t = 0; t < category; t++) {
											magnitude = (magnitude << 1) | ((currentBytes[1] >> (7 - offset)) & 1);
											offset++;
											if (offset == 8) {
												offset = 0;
												currentBytes[1] = currentBytes[0];
												bytesRead = fread(currentBytes, 1, 1, img_ptr);
												if (currentBytes[1] == 0xFF) {
													if (currentBytes[0] == 0) {
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (bytesRead != 1) {
															if (feof(img_ptr)) {
																endFlag = 1;
															}
															break;
														}
													} else {
														printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
														if (currentBytes[0] == 0xD9) {
															endImgFlag = 1;
														}
														endScanFlag = 1;
														endFlag = 1;
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
										free(node);
									} else {
										qBlocks[qBlockNum].pixels[0][0] = (int)qBlocks[qBlockNum].pixels[0][0] | (((currentBytes[1] >> (7 - offset)) & 1) << al);
										offset++;
										if (offset == 8) {
											offset = 0;
											currentBytes[1] = currentBytes[0];
											bytesRead = fread(currentBytes, 1, 1, img_ptr);
											if (currentBytes[1] == 0xFF) {
												if (currentBytes[0] == 0) {
													bytesRead = fread(currentBytes, 1, 1, img_ptr);
													if (bytesRead != 1) {
														if (feof(img_ptr)) {
															endFlag = 1;
														}
														break;
													}
												} else {
													printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
													if (currentBytes[0] == 0xD9) {
														endImgFlag = 1;;
													}
													endScanFlag = 1;
													endFlag = 1;
													break;
												}
											}
										}
									}
								}
								unsigned char nodeData = 0;
								struct huffmanNode* acTree = acTrees[currentComponent->acTable];
								struct huffmanNode* node = (struct huffmanNode*)malloc(sizeof(struct huffmanNode));
								if (se > 0) {
									int start = (ss > 0) ? ss : 1;
									for (int j = start; j <= se; j++) {
										node = copyTree(acTree);
										while (node->data == 0xFF) {
											if (((currentBytes[1] >> (7 - offset)) & 1) != 0 && (node->right)) {
												node = node->right;
											} else if (((currentBytes[1] >> (7 - offset)) & 1) == 0 && (node->left)) {
												node = node->left;
											} else {
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
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (bytesRead != 1) {
															if (feof(img_ptr)) {
																endFlag = 1;
															}
															break;
														}
													} else {
														printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
														if (currentBytes[0] == 0xD9) {
															endImgFlag = 1;
														}
														endScanFlag = 1;
														endFlag = 1;
														break;
													}
												}
											}
											nodeData = node->data;
										}
										if (endScanFlag || endImgFlag || endFlag) {
											break;
										}
										if (nodeData == 0) {
											if (ah > 0) {
												eobrun = 1;
												while (j <= se) {
													int a = j / 8;
													int b = j % 8;
													if (qBlocks[qBlockNum].pixels[a][b] != 0) {
														char refineBit = ((currentBytes[1] >> (7 - offset)) & 1);
														if (refineBit) {
															if (qBlocks[qBlockNum].pixels[a][b] > 0) {
																qBlocks[qBlockNum].pixels[a][b] += 1 << al;
															} else {
																qBlocks[qBlockNum].pixels[a][b] -= 1 << al;
															}
														}
														offset++;
														if (offset == 8) {
															offset = 0;
															currentBytes[1] = currentBytes[0];
															bytesRead = fread(currentBytes, 1, 1, img_ptr);
															if (currentBytes[1] == 0xFF) {
																if (currentBytes[0] == 0) {
																	bytesRead = fread(currentBytes, 1, 1, img_ptr);
																	if (bytesRead != 1) {
																		if (feof(img_ptr)) {
																			endFlag = 1;
																		}
																		break;
																	}
																} else {
																	printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
																	if (currentBytes[0] == 0xD9) {
																		endImgFlag = 1;
																	}
																	endScanFlag = 1;
																	endFlag = 1;
																	break;
																}
															}
														}
													}
													j++;
												}
											}
											break;
										}
										char category = nodeData & 0x0F;
										char runLength = (nodeData >> 4) & 0x0F;
										if (ah == 0 && category != 0) {
											for (int q = 0; q < runLength; q++) {
												if (j > se) {
													break;
												}
												idctBase[j / 8][j % 8] = 0;
												j++;
											}
										}
										if (ah == 0 && nodeData == 0xF0) {
											for (int q = 0; q < 16; q++) {
												if (j > se) {
													break;
												}
												idctBase[j / 8][j % 8] = 0;
												j++;
											}
											continue;
										}
										if (category == 0 && runLength != 0x0F) {
											if (progressive) {
												int v = 0;
												for (int p = 0; p < runLength; p++) {
													v = (v << 1) | ((currentBytes[1] >> (7 - offset)) & 1);
													offset++;
													if (offset == 8) {
														offset = 0;
														currentBytes[1] = currentBytes[0];
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (currentBytes[1] == 0xFF) {
															if (currentBytes[0] == 0) {
																bytesRead = fread(currentBytes, 1, 1, img_ptr);
																if (bytesRead != 1) {
																	if (feof(img_ptr)) {
																		endFlag = 1;
																	}
																	break;
																}
															} else {
																printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
																if (currentBytes[0] == 0xD9) {
																	endImgFlag = 1;
																}
																endScanFlag = 1;
																endFlag = 1;
																break;
															}
														}
													}
												}
												if (!endScanFlag) {
													eobrun = (1 << runLength) + v - 1;
													preTransBlocks[qBlockNum].componentId = currentComponent->id;
													if (ah > 0) {
														while (j <= se) {
															int a = j / 8;
															int b = j % 8;
															if (qBlocks[qBlockNum].pixels[a][b] != 0) {
																char refineBit = ((currentBytes[1] >> (7 - offset)) & 1);
																if (refineBit) {
																	if (qBlocks[qBlockNum].pixels[a][b] > 0) {
																		qBlocks[qBlockNum].pixels[a][b] += 1 << al;
																	} else {
																		qBlocks[qBlockNum].pixels[a][b] -= 1 << al;
																	}
																}
																offset++;
																if (offset == 8) {
																	offset = 0;
																	currentBytes[1] = currentBytes[0];
																	bytesRead = fread(currentBytes, 1, 1, img_ptr);
																	if (currentBytes[1] == 0xFF) {
																		if (currentBytes[0] == 0) {
																			bytesRead = fread(currentBytes, 1, 1, img_ptr);
																			if (bytesRead != 1) {
																				if (feof(img_ptr)) {
																					endFlag = 1;
																				}
																				break;
																			}
																		} else {
																			printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
																			if (currentBytes[0] == 0xD9) {
																				endImgFlag = 1;
																			}
																			endScanFlag = 1;
																			endFlag = 1;
																			break;
																		}
																	}
																}
															}
															j++;
														}
													}
													break;
												}
											} else {
												while (j < 64) {
													idctBase[j / 8][j % 8] = 0;
													j++;
												}
											}
											break;
										}
										int magnitude = 0;
										if (j >= ss && j <= se) {
											if (ah == 0) {
												for (int t = 0; t < category; t++) {
													magnitude = (magnitude << 1) | ((currentBytes[1] >> (7 - offset)) & 1);
													offset++;
													if (offset == 8) {
														offset = 0;
														currentBytes[1] = currentBytes[0];
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (currentBytes[1] == 0xFF) {
															if (currentBytes[0] == 0) {
																bytesRead = fread(currentBytes, 1, 1, img_ptr);
																if (bytesRead != 1) {
																	if (feof(img_ptr)) {
																		endFlag = 1;
																	}
																	break;
																}
															} else {
																printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
																if (currentBytes[0] == 0xD9) {
																	endImgFlag = 1;
																}
																endScanFlag = 1;
																endFlag = 1;
																break;
															}
														}
													}
												}
												if (magnitude < (1 << (category - 1))) {
													magnitude -= (1 << category) - 1;
												}
												idctBase[j / 8][j % 8] = magnitude;
											} else {
												if (category == 1) {
													int f = runLength;
													char extraBit = ((currentBytes[1] >> (7 - offset)) & 1);
													offset++;
													if (offset == 8) {
														offset = 0;
														currentBytes[1] = currentBytes[0];
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (currentBytes[1] == 0xFF) {
															if (currentBytes[0] == 0) {
																bytesRead = fread(currentBytes, 1, 1, img_ptr);
																if (bytesRead != 1) {
																	if (feof(img_ptr)) {
																		endFlag = 1;
																	}
																	break;
																}
															} else {
																printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
																if (currentBytes[0] == 0xD9) {
																	endImgFlag = 1;
																}
																endScanFlag = 1;
																endFlag = 1;
																break;
															}
														}
													}
													while ((f > 0 || qBlocks[qBlockNum].pixels[j / 8][j % 8] != 0) && j <= se) {
														if (qBlocks[qBlockNum].pixels[j / 8][j % 8] != 0) {
															int a = j / 8;
															int b = j % 8;
															char refineBit = ((currentBytes[1] >> (7 - offset)) & 1);
															if (refineBit) {
																if (qBlocks[qBlockNum].pixels[a][b] > 0) {
																	qBlocks[qBlockNum].pixels[a][b] += 1 << al;
																} else {
																	qBlocks[qBlockNum].pixels[a][b] -= 1 << al;
																}
															}
															offset++;
															if (offset == 8) {
																offset = 0;
																currentBytes[1] = currentBytes[0];
																bytesRead = fread(currentBytes, 1, 1, img_ptr);
																if (currentBytes[1] == 0xFF) {
																	if (currentBytes[0] == 0) {
																		bytesRead = fread(currentBytes, 1, 1, img_ptr);
																		if (bytesRead != 1) {
																			if (feof(img_ptr)) {
																				endFlag = 1;
																			}
																			break;
																		}
																	} else {
																		printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
																		if (currentBytes[0] == 0xD9) {
																			endImgFlag = 1;
																		}
																		endScanFlag = 1;
																		endFlag = 1;
																		break;
																	}
																}
															}
														} else {
															f--;
														}
														j++;
													}
													if (j <= se) {
														if (extraBit != 0) {
															qBlocks[qBlockNum].pixels[j / 8][j % 8] = 1 << al;
														} else {
															qBlocks[qBlockNum].pixels[j / 8][j % 8] = -(1 << al);
														}
													}
													continue;
												} else if (category == 0) {
													int f = runLength;
													if (runLength == 0x0F) {
														f++;
													}
													while (f > 0 && j <= se) {
														if (qBlocks[qBlockNum].pixels[j / 8][j % 8] != 0) {
															int a = j / 8;
															int b = j % 8;
															char refineBit = ((currentBytes[1] >> (7 - offset)) & 1);
															if (refineBit) {
																if (qBlocks[qBlockNum].pixels[a][b] > 0) {
																	qBlocks[qBlockNum].pixels[a][b] += 1 << al;
																} else {
																	qBlocks[qBlockNum].pixels[a][b] -= 1 << al;
																}
															}
															offset++;
															if (offset == 8) {
																offset = 0;
																currentBytes[1] = currentBytes[0];
																bytesRead = fread(currentBytes, 1, 1, img_ptr);
																if (currentBytes[1] == 0xFF) {
																	if (currentBytes[0] == 0) {
																		bytesRead = fread(currentBytes, 1, 1, img_ptr);
																		if (bytesRead != 1) {
																			if (feof(img_ptr)) {
																				endFlag = 1;
																			}
																			break;
																		}
																	} else {
																		printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
																		if (currentBytes[0] == 0xD9) {
																			endImgFlag = 1;
																		}
																		endScanFlag = 1;
																		endFlag = 1;
																		break;
																	}
																}
															}
														} else {
															f--;
														}
														j++;
													}
													continue;
												}
											}
										}
									}
								}		
								for (int j = se + 1; j < 64; j++) {
									idctBase[j / 8][j % 8] = 0;
								}
								if (!progressive || (se == 0 && ah == 0)) {
									for (int a = 0; a < 8; a++) {
										for (int b = 0; b < 8; b++) {
											idctBase[a][b] <<= al;
										}
									}
									if (!progressive) {
										for (int a = 0; a < 8; a++) {
											for (int b = 0; b < 8; b++) {
												idctBase[a][b] = idctBase[a][b] * qt->data[a][b];
											}
										}
									}
									for (int a = 0; a < 8; a++) {
										for (int b = 0; b < 8; b++) {
											qBlocks[qBlockNum].pixels[a][b] = idctBase[a][b];
											if (!progressive) {
												preTransBlocks[qBlockNum].pixels[a][b] = idctBase[zigzag[a][b] / 8][zigzag[a][b] % 8];
											}
										}
									}
								} else {
									if (ah == 0) {
										for (int index = ss; index <= se; index++) {
											int a = index / 8;
											int b = index % 8;
											qBlocks[qBlockNum].pixels[a][b] = idctBase[a][b] << al;
										}
									}
								}
								if (progressive) {
									for (int index = 0; index < 64; index++) {
										int a = index / 8;
										int b = index % 8;
										preTransBlocks[qBlockNum].pixels[a][b] = qBlocks[qBlockNum].pixels[zigzag[a][b] / 8][zigzag[a][b] % 8] * qt->data[zigzag[a][b] / 8][zigzag[a][b] % 8];
									}
								}
								preTransBlocks[qBlockNum].componentId = currentComponent->id;
								free(node);
							} else {
								if (ah > 0) {
									for (int index = ss; index <= se; index++) {
										int a = index / 8;
										int b = index % 8;
										if (qBlocks[qBlockNum].pixels[a][b] != 0) {
											char refineBit = ((currentBytes[1] >> (7 - offset)) & 1);
											if (refineBit) {
												if (qBlocks[qBlockNum].pixels[a][b] > 0) {
													qBlocks[qBlockNum].pixels[a][b] += 1 << al;
												} else {
													qBlocks[qBlockNum].pixels[a][b] -= 1 << al;
												}
											}
											offset++;
											if (offset == 8) {
												offset = 0;
												currentBytes[1] = currentBytes[0];
												bytesRead = fread(currentBytes, 1, 1, img_ptr);
												if (currentBytes[1] == 0xFF) {
													if (currentBytes[0] == 0) {
														bytesRead = fread(currentBytes, 1, 1, img_ptr);
														if (bytesRead != 1) {
															if (feof(img_ptr)) {
																endFlag = 1;
															}
															break;
														}
													} else {
														printf("Marker found: %x, address: %x\n", currentBytes[1] << 8 | currentBytes[0], ftell(img_ptr));
														if (currentBytes[0] == 0xD9) {
															endImgFlag = 1;
														}
														endScanFlag = 1;
														endFlag = 1;
														break;
													}
												}
											}
										}
									}
								}
								for (int index = 0; index < 64; index++) {
									int a = index / 8;
									int b = index % 8;
									preTransBlocks[qBlockNum].pixels[a][b] = qBlocks[qBlockNum].pixels[zigzag[a][b] / 8][zigzag[a][b] % 8] * qt->data[zigzag[a][b] / 8][zigzag[a][b] % 8];
								}
								preTransBlocks[qBlockNum].componentId = currentComponent->id;
								eobrun--;
							}
							if (numComponentsScan == 1) {
								if (currentComponent->id == 1) {
									qBlockNum = x + 1;
								} else if (currentComponent->id == 2) {
									qBlockNum = totalYBlocks + x + 1;
								} else if (currentComponent->id == 3) {
									qBlockNum = totalYBlocks + totalCbBlocks + x + 1;
								}
							} else if (numComponentsScan == 3) {
								int mcuPos = (x + 1) % (sfy + sfcbh * sfcbv + sfcrh * sfcrv);
								if (mcuPos == 0 || mcuPos == sfy || mcuPos == sfy + sfcbh * sfcbv) {
									currentComponent = components[currentComponent->id % numComponents];
								}
								int mcuNum = (x + 1) / (sfy + sfcbh * sfcbv + sfcrh * sfcrv);
								int loc = (x + 1) % (sfy + sfcbh * sfcbv + sfcrh * sfcrv);
								int mcuCols = (yBlocksPerRow + sfyh - 1) / sfyh;
								if (currentComponent->id == 1) {
									qBlockNum = (mcuNum / mcuCols * sfyv + loc / sfyh) * yBlocksPerRow + (mcuNum % mcuCols * sfyh) + loc % sfyh;
									if (qBlockNum >= totalYBlocks) {
										break;
									}
								} else if (currentComponent->id == 2) {
									qBlockNum = totalYBlocks + (x + 1) / (sfy + sfcbh * sfcbv + sfcrh * sfcrv);
									if (qBlockNum >= totalYBlocks + totalCbBlocks) {
										break;
									}
								} else if (currentComponent->id == 3) {
									qBlockNum = totalYBlocks + totalCbBlocks + (x + 1) / (sfy + sfcbh * sfcbv + sfcrh * sfcrv);
									if (qBlockNum >= totalBlocks) {
										break;
									}
								}
							}
						} else {
							printf("end of file found\n");
							break;
						}
						fflush(stdout);
					} else {
						printf("end of scan\n");					
						break;
					}
				}
				printf("Scan ended at %x\n", ftell(img_ptr));
				fflush(stdout);
				for (int x = 0; x < totalBlocks; x++) {
					for (int a = 0; a < 8; a++) {
						for (int b = 0; b < 8; b++) {
							float localSum = 0.0;
							for (int u = 0; u < 8; u++) {
								for (int v = 0; v < 8; v++) {
									float normCoeff = 1.0;
									if (u == 0) {
										normCoeff *= 1.0 / sqrt(2.0);
									}
									if (v == 0) {
										normCoeff *= 1.0 / sqrt(2.0);
									}
									localSum += normCoeff * preTransBlocks[x].pixels[u][v] * idctTable[u][a] * idctTable[v][b];
								}
							}
							out[x].pixels[a][b] = round(localSum / 4.0) + 128.0;
						}
					}
					out[x].componentId = preTransBlocks[x].componentId;
				}
				for (int yBlock = 0; yBlock < totalYBlocks; yBlock++) {
					int yBlockX = yBlock % yBlocksPerRow;
					int yBlockY = yBlock / yBlocksPerRow;
					int cbBlock = (yBlockY / cbRatioV) * cbBlocksPerRow + yBlockX / cbRatioH;
					int crBlock = (yBlockY / crRatioV) * crBlocksPerRow + yBlockX / crRatioH;
					for (int index = 0; index < 64; index++) {
						int yPosX = index % 8;
						int yPosY = index / 8;
						float Y = out[yBlock].pixels[yPosX][yPosY];
						int cbPosX = ((yBlockY * 8 + yPosX) / cbRatioH) % 8;
						int cbPosY = ((yBlockX * 8 + yPosY) / cbRatioV) % 8;
						float Cb = out[totalYBlocks + cbBlock].pixels[cbPosX][cbPosY];
						int crPosX = ((yBlockY * 8 + yPosX) / crRatioH) % 8;
						int crPosY = ((yBlockX * 8 + yPosY) / crRatioV) % 8;
						float Cr = out[totalYBlocks + totalCbBlocks + crBlock].pixels[crPosX][crPosY];
						float R = Y + 1.402 * (Cr - 128.0);
						float G = Y - 0.344136 * (Cb - 128.0) - 0.714136 * (Cr - 128.0);
						float B = Y + 1.772 * (Cb - 128.0);
						imgBlocks[yBlock].pixelsR[yPosX][yPosY] = (unsigned char)(R < 0 ? 0 : R > 255 ? 255 : round(R));
						imgBlocks[yBlock].pixelsG[yPosX][yPosY] = (unsigned char)(G < 0 ? 0 : G > 255 ? 255 : round(G));
						imgBlocks[yBlock].pixelsB[yPosX][yPosY] = (unsigned char)(B < 0 ? 0 : B > 255 ? 255 : round(B));
					}
				}
				linearizedImg = malloc(3 * height * width);
				if (!linearizedImg) {
					printf("allocation failed\n");
				}
				int count = 0;
				for (int y = 0; y < yBlocksPerCol; y++) {
					for (int y2 = 0; y2 < 8; y2++) {
						for (int x = 0; x < yBlocksPerRow; x++) {
							int blockPos = yBlocksPerRow * y + x;
							for (int x2 = 0; x2 < 8; x2++) {
								unsigned char rValue = imgBlocks[blockPos].pixelsR[y2][x2];
								unsigned char gValue = imgBlocks[blockPos].pixelsG[y2][x2];
								unsigned char bValue = imgBlocks[blockPos].pixelsB[y2][x2];
								linearizedImg[count++] = rValue;
								linearizedImg[count++] = gValue;
								linearizedImg[count++] = bValue;
							}
						}
					}
				}
				SDL_UpdateTexture(texture, NULL, linearizedImg, 3 * width);
				SDL_RenderClear(renderer);
				SDL_RenderTexture(renderer, texture, NULL, NULL);
				SDL_RenderPresent(renderer);
				SDL_Delay(1000);
				free(linearizedImg);
				do {
					fseek(img_ptr, -1L, SEEK_CUR);
					fread(currentBytes, 1, 1, img_ptr);
					fseek(img_ptr, -1L, SEEK_CUR);
				} while (currentBytes[0] != 0xFF);
			} else if (value == endOfImage) {
				break;
			} else {
				currentBytes[1] = currentBytes[0];
			}
		}
	}
	fflush(stdout);
	fclose(img_ptr);
	getchar();
	SDL_DestroyWindow(window);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyTexture(texture);
	SDL_Quit();
	return 0;
}
