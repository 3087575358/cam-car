import gzip
import re

def extract_compressed_data_from_header(header_file):
    """
    从 camera_index.h 文件中提取所有压缩数据

    Args:
        header_file: camera_index.h 文件路径

    Returns:
        包含所有压缩数据的列表，每个元素是 (数组名, 输出文件名, 数据) 元组
    """
    with open(header_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # 查找所有压缩数据块
    # 格式: index_ov2640_html_gz_len 6787
    #       const uint8_t index_ov2640_html_gz[] = { ... };
    pattern = r'#define\s+(\w+_len)\s+(\d+)\s*\n\s*const\s+uint8_t\s+(\w+)\[\]\s*=\s*\{(.*?)\};'

    matches = re.findall(pattern, content, re.DOTALL)

    compressed_data = []
    for len_define, length, array_name, hex_data in matches:
        # 从数组名提取输出文件名，例如: index_ov2640_html_gz -> index_ov2640.html
        output_file = array_name.replace('_html_gz', '.html').replace('_', '_')

        # 解析十六进制数据
        hex_values = re.findall(r'0x[0-9A-Fa-f]{2}', hex_data)
        if len(hex_values) != int(length):
            print(f"警告: {array_name} 声明长度 {length} 与实际数据 {len(hex_values)} 不匹配")

        # 转换为字节数据
        gz_data = bytes(int(h, 16) for h in hex_values)

        compressed_data.append((array_name, output_file, gz_data))

    return compressed_data

def decompress_data(gz_data, output_filename):
    """
    解压缩数据并写入文件

    Args:
        gz_data: 压缩的字节数据
        output_filename: 输出文件名

    Returns:
        0 表示成功，-1 表示失败
    """
    try:
        # 使用 gzip 解压
        decompressed = gzip.decompress(gz_data)

        # 写入输出文件
        with open(output_filename, 'wb') as f:
            f.write(decompressed)

        print(f"成功解压到 {output_filename} ({len(decompressed)} 字节)")
        return 0

    except gzip.BadGzipFile:
        print(f"错误: {output_filename} 的数据不是有效的 gzip 格式")
        return -1
    except Exception as e:
        print(f"解压 {output_filename} 时出错: {str(e)}")
        return -1

def main():
    header_file = 'camera_index.h'

    # 提取所有压缩数据
    print(f"正在从 {header_file} 提取压缩数据...")
    compressed_data_list = extract_compressed_data_from_header(header_file)

    if not compressed_data_list:
        print(f"错误: 未在 {header_file} 中找到任何压缩数据")
        return -1

    print(f"找到 {len(compressed_data_list)} 个压缩数据块")

    # 解压每个数据块
    success_count = 0
    for array_name, output_filename, gz_data in compressed_data_list:
        print(f"\n处理 {array_name} -> {output_filename}")
        result = decompress_data(gz_data, output_filename)
        if result == 0:
            success_count += 1

    # 输出总结
    print(f"\n解压完成: 成功 {success_count}/{len(compressed_data_list)}")
    return 0 if success_count == len(compressed_data_list) else -1

if __name__ == "__main__":
    exit(main())
