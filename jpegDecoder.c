#include <stdio.h>
#include <math.h>
#include <SDL3-devel-3.2.8-VC\SDL3-3.2.8\include\SDL3>

struct huffmanNode {
    unsigned char data;
    struct huffmanNode* left, * right;
};

struct quantTable {
    char data[8][8];
};

struct component {
    char id;
    char samplingFactors;
    char quantTable;
    char oldDC;
};

struct huffmanNode* copyTree(struct huffmanNode* tree) {
    struct huffmanNode* newTree = (struct huffmanNode*)malloc(sizeof(struct huffmanNode));
    newTree->data = tree->data;
    //printf("node copied\n");
    if (tree->left) {
        newTree->left = copyTree(tree->left);
    }
    if (tree->right) {
        newTree->right = copyTree(tree->right);
    }
    return newTree;
}

int addToTree(struct huffmanNode* node, char element, int pos) {
    printf("adding %d to tree at %x at position %d\n", element, &node, pos);
    struct huffmanNode* newNode = (struct huffmanNode*)malloc(sizeof(struct huffmanNode));
    newNode->data = 0xFF;
    newNode->left = NULL;
    newNode->right = NULL;
    if (pos == 0) {
        newNode->data = element;
        if (!(node->left)) {
            printf("adding %d to tree on left\n", element);
            node->left = newNode;
            return 0;
        }
        else if (!(node->right)) {
            printf("adding %d to tree on right\n", element);
            node->right = newNode;
            return 0;
        }
        else {
            printf("tree is full, backtracking\n");
            return 1;
        }
    }
    else {
        int x;
        if (!(node->left)) {
            printf("making empty node on left\n");
            node->left = newNode;
            x = addToTree(newNode, element, pos - 1);
            if (x == 0) {
                return x;
            }
        }
        if (!(node->right)) {
            printf("making empty node on right\n");
            node->right = newNode;
            printf("empty node at %x\n", node->right);
            x = addToTree(newNode, element, pos - 1);
            if (x == 0) {
                return x;
            }
        }
        if (node->left->data == 0xFF) {
            printf("going to the left\n");
            x = addToTree(node->left, element, pos - 1);
            if (x == 0) {
                return x;
            }
        }
        if (node->right->data == 0xFF) {
            printf("going to the right\n");
            x = addToTree(node->right, element, pos - 1);
            if (x == 0) {
                return x;
            }
        }
        else {
            printf("right node data %x\n", node->right->data);
        }
        printf("nowhere to go\n");
        return 1;
    }
    return 0;
}

struct huffmanNode* createTreeFromLengths(char* lengths, char* elements) {
    struct huffmanNode* root = (struct huffmanNode*)malloc(sizeof(struct huffmanNode));
    root->data = 0xFF;
    root->left = NULL;
    root->right = NULL;
    int count = 0;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < lengths[i]; j++) {
            printf("adding %d to tree at %x, count = %d, position = %d\n", elements[count], &root, count, i);
            int success = addToTree(root, elements[count], i);
            if (success == 0) {
                printf("successful\n");
            }
            else {
                printf("unsuccessful\n");
            }
            count++;
        }
    }
    return root;
}

void printCodes(struct huffmanNode* root, int arr[], int top) {
    // Assign 0 to left edge and recur 
    if (root->left && root->right) {
        arr[top] = 0;
        //printf("going left\n");
        printCodes(root->left, arr, top + 1);
        arr[top] = 1;
        //printf("going right\n");
        printCodes(root->right, arr, top + 1);
    }
    else if (root->left) {
        arr[top] = 0;
        //printf("going left\n");
        printCodes(root->left, arr, top + 1);
    }
    else if (root->right) {
        arr[top] = 1;
        //printf("going right\n");
        printCodes(root->right, arr, top + 1);
    }

    // Assign 1 to right edge and recur 

    // If this is a leaf node, then 
    // it contains one of the input 
    // characters, print the character 
    // and its code from arr[] 
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
    const int huffmanTable = 0xFFC4;
    const int quantTable = 0xFFDB;
    const int startOfScan = 0xFFDA;
    const char zigzag[8][8] =
    { {0, 1, 5, 6, 14, 15, 27, 28},
    {2, 4, 7, 13, 16, 26, 29, 42},
    {3, 8, 12, 17, 25, 30, 41, 43},
    {9, 11, 18, 24, 31, 40, 44, 53},
    {10, 19, 23, 32, 39, 45, 52, 54},
    {20, 22, 33, 38, 46, 51, 55, 60},
    {21, 34, 37, 47, 50, 56, 59, 61},
    {35, 36, 48, 49, 57, 58, 62, 63} };
    char rearranged[8][8];
    double idctTable[8][8];
    char numComponents;
    struct component* components[3];
    int idctPrecision = 8;
    int treeCount = 0;
    int Y, Cb, Cr;
    unsigned short height, width;
    struct huffmanNode* huffmanTrees[8];
    struct quantTable** qtables;
    unsigned char currentBytes[2];
    char lengthBytes[2];
    size_t bytesRead;
    char lengths[16];
    FILE* img_ptr;
    char* fileName = argv[1];

    img_ptr = fopen(fileName, "rb");
    if (img_ptr == NULL) {
        perror("Error opening file");
        return 1;
    }
    while ((bytesRead = fread(currentBytes, 1, 1, img_ptr)) > 0) {
        if (bytesRead == 1) {
            unsigned short value = (unsigned char)currentBytes[1] << 8 | ((unsigned char)currentBytes[0]);
            if (value == huffmanTable) {
                printf("huffman table found\n");
                bytesRead = fread(currentBytes, 1, 2, img_ptr);
                unsigned short length = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
                printf("length = %d\n", length);
                bytesRead = fread(currentBytes, 1, 1, img_ptr);
                char htinfo = currentBytes[0];
                printf("htinfo = %02x\n", htinfo);
                int numElements = 0;
                bytesRead = fread(lengths, 1, 16, img_ptr);
                for (int i = 0; i < 16; i++) {
                    printf("number of codes length %d = %d\n", i + 1, lengths[i]);
                    numElements += lengths[i];
                }
                printf("elements = %d\n", numElements);
                char elements[numElements];
                bytesRead = fread(elements, 1, numElements, img_ptr);
                for (int i = 0; i < numElements; i++) {
                    printf("elements[%d] = %d\n", i, elements[i]);
                }
                struct huffmanNode* tree = createTreeFromLengths(lengths, elements);
                int arr[16];
                printCodes(tree, arr, 0);
                huffmanTrees[treeCount] = tree;
                treeCount++;
            }
            else if (value == quantTable) {
                printf("quant table found\n");
                bytesRead = fread(currentBytes, 1, 2, img_ptr);
                unsigned short length = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
                printf("length = %d\n", length);
                bytesRead = fread(currentBytes, 1, 1, img_ptr);
                char qtinfo = currentBytes[0];
                printf("qtinfo = %02x\n", qtinfo);
                qtables = malloc(64 * length / 64);
                printf("there are %d quant tables\n", length / 64);
                for (int i = 0; i < length / 64; i++) {
                    struct quantTable* qt = (struct quantTable*)malloc(sizeof(struct quantTable));
                    for (int j = 0; j < 8; j++) {
                        for (int k = 0; k < 8; k++) {
                            bytesRead = fread(currentBytes, 1, 1, img_ptr);
                            qt->data[j][k] = currentBytes[0];
                        }
                    }
                    qtables[i] = qt;
                }
                for (int i = 0; i < 8; i++) {
                    for (int j = 0; j < 8; j++) {
                        printf("%d ", qtables[1]->data[i][j]);
                    }
                    printf("\n");
                }
            }
            else if (value == startOfFrame0) {
                printf("start of frame (0)\n");
                bytesRead = fread(currentBytes, 1, 2, img_ptr);
                unsigned short length = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
                printf("length = %d\n", length);
                bytesRead = fread(currentBytes, 1, 1, img_ptr);
                char precision = currentBytes[0];
                printf("precision = %d bits\n", precision);
                bytesRead = fread(currentBytes, 1, 2, img_ptr);
                height = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
                printf("image height = %d\n", height);
                bytesRead = fread(currentBytes, 1, 2, img_ptr);
                width = (unsigned char)currentBytes[0] << 8 | ((unsigned char)currentBytes[1]);
                printf("image width = %d\n", width);
                bytesRead = fread(currentBytes, 1, 1, img_ptr);
                numComponents = currentBytes[0];
                printf("number of components: %d\n", numComponents);
                for (int i = 0; i < numComponents; i++) {
                    struct component* newComponent = (struct component*)malloc(sizeof(struct component));
                    bytesRead = fread(currentBytes, 1, 1, img_ptr);
                    newComponent->id = currentBytes[0];
                    bytesRead = fread(currentBytes, 1, 1, img_ptr);
                    newComponent->samplingFactors = currentBytes[0];
                    bytesRead = fread(currentBytes, 1, 1, img_ptr);
                    newComponent->quantTable = currentBytes[0];
                    newComponent->oldDC = 0;
                    components[i] = newComponent;
                    printf("component id: %d, sampling: %d, quant table %d\n", components[i]->id, components[i]->samplingFactors, components[i]->quantTable);
                }
            }
            else if (value == startOfScan) {
                printf("start of scan\n");
                for (int u = 0; u < idctPrecision; u++) {
                    for (int x = 0; x < idctPrecision; x++) {
                        double normCoeff = 1.0;
                        if (u == 0) {
                            normCoeff = 1.0 / sqrt(2.0);
                        }
                        idctTable[u][x] = normCoeff * cos(((2.0 * x + 1.0) * u * 3.14159) / 16.0);
                        printf("idct table entry[%d][%d] = %f\n", u, x, idctTable[u][x]);
                    }
                }
                printf("idct table created\n");
                for (int y = 0; y < height / 8; y++) {
                    for (int x = 0; x < width / 8; x++) {
                        char out[numComponents][8][8];
                        for (int i = 0; i < numComponents; i++) {
                            char idctBase[8][8];
                            struct component* currentComponent = components[i];
                            struct huffmanNode* tree = huffmanTrees[0];
                            bytesRead = fread(currentBytes, 1, 1, img_ptr);
                            currentBytes[1] = currentBytes[0];
                            bytesRead = fread(currentBytes, 1, 1, img_ptr);
                            if (bytesRead != 1) {
                                if (feof(img_ptr)) {
                                    printf("end of file\n");
                                }
                                break;
                            }
                            char offset = 0;
                            struct huffmanNode* node = copyTree(tree);
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
                                    if (currentBytes[1] == 0xFF && currentBytes[0] == 0) {
                                        bytesRead = fread(currentBytes, 1, 1, img_ptr);
                                    }
                                }
                            }
                            printf("old dc: %d\n", currentComponent->oldDC);
                            int newdc = currentComponent->oldDC + node->data;
                            printf("new dc: %d\n", newdc);
                            idctBase[0][0] = newdc;
                            currentComponent->oldDC = newdc;
                            for (int j = 1; j < 64; j++) {
                                node = copyTree(tree);
                                while (node->data == 0xFF) {
                                    if (((currentBytes[1] >> (7 - offset)) & 1) != 0 && (node->right)) {
                                        node = node->right;
                                    }
                                    else if (node->left) {
                                        node = node->left;
                                    }
                                    else {
                                        while (j < 64) {
                                            idctBase[j / 8][j % 8] = 0;
                                            j++;
                                        }
                                        printf("end of block\n");
                                    }
                                    offset++;
                                    if (offset == 8) {
                                        offset = 0;
                                        currentBytes[1] = currentBytes[0];
                                        bytesRead = fread(currentBytes, 1, 1, img_ptr);
                                        if (currentBytes[1] == 0xFF && currentBytes[0] == 0) {
                                            bytesRead = fread(currentBytes, 1, 1, img_ptr);
                                        }
                                    }
                                }
                                printf("%d\n", node->data);
                                printf("%d\n", offset);
                                if (node->data == 0) {
                                    while (j < 64) {
                                        idctBase[j / 8][j % 8] = 0;
                                        j++;
                                    }
                                    printf("end of block\n");
                                }
                                else if (node->data == 0xF0) {
                                    for (int k = 0; k < 16; k++) {
                                        idctBase[j / 8][j % 8] = 0;
                                        j++;
                                    }
                                    printf("16 0s\n");
                                }
                                int qtnum = currentComponent->quantTable;
                                struct quantTable* qt = qtables[qtnum];
                                idctBase[j / 8][j % 8] = node->data * qt->data[j / 8][j % 8];
                            }
                            printf("base\n");
                            for (int a = 0; a < 8; a++) {
                                for (int b = 0; b < 8; b++) {
                                    printf("%d ", idctBase[a][b]);
                                }
                                printf("\n");
                            }
                            printf("\n");
                            printf("rearranged\n");
                            for (int a = 0; a < 8; a++) {
                                for (int b = 0; b < 8; b++) {
                                    rearranged[a][b] = idctBase[zigzag[a][b] / 8][zigzag[a][b] % 8];
                                    printf("%d ", rearranged[a][b]);
                                }
                                printf("\n");
                            }
                            printf("\n");
                            printf("out[%d]\n", i);

                            for (int a = 0; a < 8; a++) {
                                for (int b = 0; b < 8; b++) {
                                    int localSum = 0;
                                    for (int u = 0; u < idctPrecision; u++) {
                                        for (int v = 0; v < idctPrecision; v++) {
                                            localSum += rearranged[v][u] * idctTable[u][a] * idctTable[v][b];
                                        }
                                    }
                                    out[i][a][b] = localSum / 4;
                                    printf("%d ", out[i][a][b]);
                                }
                                printf("\n");
                            }
                        }
                        if (numComponents == 3) {
                            for (int yy = 0; yy < 8; yy++) {
                                for (int xx = 0; xx < 8; xx++) {
                                    char Y = out[0][xx][yy];
                                    char Cb = out[1][xx][yy];
                                    char Cr = out[2][xx][yy];
                                    char R = Cr * (2.0 - 2.0 * .299) + Y + 128;
                                    char B = Cb * (2.0 - 2.0 * .114) + Y + 128;
                                    char G = (Y - .114 * B - .299 * R) / .587 + 128;
                                }
                            }
                        }
                    }
                }


            }
            else {
                currentBytes[1] = currentBytes[0];
            }
        }
    }
    fclose(img_ptr);
    return 0;
}