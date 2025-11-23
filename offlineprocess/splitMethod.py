
import os

def find_file(filename, directory):
    files = [f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory,f))]
    if filename in files:
        full_path = os.path.join(directory, filename)
        print(f"Found: {full_path}")
        return full_path
    print("File not found.")
    return None



def split_file_by_method_tag(content, methods_per_file):
    parts = content.split("</method>")
    total = len(parts) - 1  # Last part might not be a full method
    file_count = 0

    for i in range(0, total, methods_per_file):
        chunk = parts[i:i + methods_per_file]
        if not chunk:
            continue

        # Reattach the </method> tag to each block
        chunk_with_tags = ["</method>".join([part]) + "</method>" for part in chunk]

        output_content = "".join(chunk_with_tags)
        output_filename = f"agent-trace-method{file_count + 1}.run"
        with open(output_filename, "w", encoding="utf-8") as out_file:
            out_file.write(output_content)

        file_count += 1

    print(f"Split into {file_count} files.")



def main():
    full_path = find_file("agent-trace-method.run", ".")
    with open(full_path, "r", encoding="utf-8") as file:
        content = file.read()
    count = content.count("</method>")
    print(f"Number of </method> tags: {count}")
    # Run it
    split_file_by_method_tag(content, int(count/96) + 1)

main()