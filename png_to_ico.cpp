#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#pragma warning(disable: 4244)

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#include <iostream>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include "include/stb_image.h"
#include "include/stb_image_write.h"
#include <string>
#include <vector>
#include <algorithm>
#include <shellapi.h>

bool FileExists(const std::string& filename) {
    DWORD fileAttr = GetFileAttributesA(filename.c_str());
    return (fileAttr != INVALID_FILE_ATTRIBUTES && !(fileAttr & FILE_ATTRIBUTE_DIRECTORY));
}

std::string GetUniqueFileName(const std::string& path) {
    std::string newPath = path;
    int counter = 1;
    while (FileExists(newPath)) {
        size_t dotPos = path.find_last_of('.');
        std::string baseName = path.substr(0, dotPos);
        std::string extension = path.substr(dotPos);
        newPath = baseName + "(" + std::to_string(counter++) + ")" + extension;
    }
    return newPath;
}

void write_le16(FILE* file, uint16_t value) {
    fputc(value & 0xFF, file);
    fputc((value >> 8) & 0xFF, file);
}

void write_le32(FILE* file, uint32_t value) {
    fputc(value & 0xFF, file);
    fputc((value >> 8) & 0xFF, file);
    fputc((value >> 16) & 0xFF, file);
    fputc((value >> 24) & 0xFF, file);
}

int min_val(int a, int b) {
    return (a < b) ? a : b;
}

std::vector<unsigned char> resizeImage(const unsigned char* data, int srcW, int srcH, int targetW, int targetH) {
    std::vector<unsigned char> resized(targetW * targetH * 4);

    float scaleX = (float)srcW / targetW;
    float scaleY = (float)srcH / targetH;

    for (int y = 0; y < targetH; y++) {
        for (int x = 0; x < targetW; x++) {
            float srcX = x * scaleX;
            float srcY = y * scaleY;

            int x1 = (int)srcX;
            int y1 = (int)srcY;
            int x2 = min_val(x1 + 1, srcW - 1);
            int y2 = min_val(y1 + 1, srcH - 1);

            float fx = srcX - x1;
            float fy = srcY - y1;

            for (int c = 0; c < 4; c++) {
                float p1 = data[(y1 * srcW + x1) * 4 + c] * (1 - fx) + data[(y1 * srcW + x2) * 4 + c] * fx;
                float p2 = data[(y2 * srcW + x1) * 4 + c] * (1 - fx) + data[(y2 * srcW + x2) * 4 + c] * fx;
                float final = p1 * (1 - fy) + p2 * fy;

                resized[(y * targetW + x) * 4 + c] = (unsigned char)(final + 0.5f);
            }
        }
    }

    return resized;
}

int stbi_write_ico(const char* filename, int w, int h, int comp, const void* data) {
    if (comp != 4) return 0;

    FILE* file = nullptr;
    if (fopen_s(&file, filename, "wb") != 0 || !file) {
        return 0;
    }

    std::vector<int> sizes;
    int originalSize = min_val(w, h);

    if (originalSize >= 256) {
        sizes.push_back(originalSize);
    }

    if (originalSize > 256) {
        sizes.push_back(256);
    }

    if (originalSize < 256) {
        sizes.push_back(256);
    }

    std::sort(sizes.rbegin(), sizes.rend());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());

    int numImages = (int)sizes.size();

    write_le16(file, 0);
    write_le16(file, 1);
    write_le16(file, numImages);

    uint32_t dataOffset = 6 + (numImages * 16);

    std::vector<std::vector<unsigned char>> pngData;
    std::vector<uint32_t> pngSizes;

    for (int size : sizes) {
        std::vector<unsigned char> imageData;

        if (size == originalSize && w == h) {
            imageData.assign((unsigned char*)data, (unsigned char*)data + (w * h * 4));
        }
        else {
            imageData = resizeImage((unsigned char*)data, w, h, size, size);
        }

        char tempFile[MAX_PATH];
        sprintf_s(tempFile, "temp_ico_%d_%d.png", size, GetTickCount());

        stbi_write_png_compression_level = 0;
        if (stbi_write_png(tempFile, size, size, 4, imageData.data(), size * 4)) {
            FILE* pngFile = nullptr;
            if (fopen_s(&pngFile, tempFile, "rb") == 0 && pngFile) {
                fseek(pngFile, 0, SEEK_END);
                long fileSize = ftell(pngFile);
                fseek(pngFile, 0, SEEK_SET);

                std::vector<unsigned char> png(fileSize);
                fread(png.data(), 1, fileSize, pngFile);
                fclose(pngFile);

                pngData.push_back(png);
                pngSizes.push_back((uint32_t)fileSize);
            }
            DeleteFileA(tempFile);
        }
    }

    for (int i = 0; i < numImages; i++) {
        int size = sizes[i];

        fputc(size >= 256 ? 0 : size, file);
        fputc(size >= 256 ? 0 : size, file);
        fputc(0, file);
        fputc(0, file);
        write_le16(file, 1);
        write_le16(file, 32);
        write_le32(file, pngSizes[i]);
        write_le32(file, dataOffset);

        dataOffset += pngSizes[i];
    }

    for (const auto& png : pngData) {
        fwrite(png.data(), 1, png.size(), file);
    }

    fclose(file);
    return 1;
}

int main() {
    OPENFILENAME ofn;
    wchar_t szFile[260];

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = L'\0';
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);

    ofn.lpstrFilter = L"Image Files\0*.PNG;*.JPG;*.JPEG;*.WEBP;*.BMP;*.GIF;*.TGA;*.PSD;*.HDR;*.PIC\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.lpstrTitle = L"Select image file";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn) == TRUE) {
        std::wstring wFilePath(ofn.lpstrFile);
        std::string filePath(wFilePath.begin(), wFilePath.end());

        int width, height, channels;
        unsigned char* image = stbi_load(filePath.c_str(), &width, &height, &channels, 4);

        if (image != nullptr) {
            size_t lastDot = filePath.find_last_of('.');
            std::string baseName = (lastDot != std::string::npos) ? filePath.substr(0, lastDot) : filePath;
            std::string outputFile = GetUniqueFileName(baseName + ".ico");

            if (stbi_write_ico(outputFile.c_str(), width, height, 4, image)) {
                std::wstring wOutputFile(outputFile.begin(), outputFile.end());
                ShellExecute(NULL, L"open", L"explorer.exe",
                    (L"/select,\"" + wOutputFile + L"\"").c_str(),
                    NULL, SW_SHOWNORMAL);
            }

            stbi_image_free(image);
        }
    }

    return 0;
}