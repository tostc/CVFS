#include <iostream>
#include <VFS.hpp>
#include <fstream>

using namespace std;

void PrintDirs(VFS::CVFS &vfs, const std::string &Path, const std::string &Shift = "")
{
	auto node = vfs.GetNodeInfo(Path);
	auto childs = vfs.List(node);

	cout << Shift << "Dir: " << node->Name() << endl;
	for (auto &&i : childs)
	{
		if(i->IsDir())
			PrintDirs(vfs, Path + i->Name() + "/", Shift + " ");
		else
			cout << Shift << " File: " << i->Name() << " Size: " << vfs.FileSize(i) << endl;
	}
}

int main()
{
	VFS::CVFS vfs;

	//Linux like file hierarchy.
	vfs.CreateDir("/bin");
	vfs.CreateDir("/boot");
	vfs.CreateDir("/dev");
	vfs.CreateDir("/etc");
	vfs.CreateDir("/home");
	vfs.CreateDir("/lib");
	vfs.CreateDir("/media");
	vfs.CreateDir("/mnt");
	vfs.CreateDir("/opt");
	vfs.CreateDir("/sbin");
	vfs.CreateDir("/srv");
	vfs.CreateDir("/tmp");
	vfs.CreateDir("/usr");
	vfs.CreateDir("/proc");

	vfs.CreateDir("/tmp/Test");

	//Reads the header file into the filesystem.
	ifstream in("VFS.hpp", ios::in);
	if(in.is_open())
	{
		auto fs = vfs.Open("/tmp/VFS.txt", VFS::FileMode::RW);

		while (in.good())
		{
			char c;
			in.read(&c, sizeof(c));

			if(in.good())
				fs->Write(&c, sizeof(c));
		}

		while (!fs->IsEOF())
		{
			cout << fs->ReadLine() << endl;
		}

		fs->Seek(VFS::Cursor::END, -3);
		cout << fs->Read() << endl;

		in.close();

		vfs.Rename("/tmp/VFS.txt", "AVFS.hpp");
		vfs.Move("/tmp/AVFS.hpp", "/usr");
		vfs.Delete("/tmp/Test");
		vfs.Copy("/usr/AVFS.hpp", "/tmp/AVFS.hpp");
		vfs.Copy("tmp", "usr/tmp_copy");
	}

	PrintDirs(vfs, "/");

	return 0;
}
