using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;
using Windows.UI.Core;
using Windows.Storage;
using Windows.Storage.AccessCache;
using Windows.Storage.Pickers;
using SQLiteWinRTExtensions;


// The Blank Page item template is documented at http://go.microsoft.com/fwlink/?LinkId=234238

namespace SQLiteWinRTExtesnionsSample
{
    public class DatabaseFile
    {
        public string Path { get; set; }
        public string DisplayName { get; set; }
    }

    public class ViewModel
    {
        ObservableCollection<DatabaseFile> _DatabaseFiles = new ObservableCollection<DatabaseFile>();
        public ObservableCollection<DatabaseFile> DatabaseFiles
        {
            get
            {
                return _DatabaseFiles;
            }
        }

        ObservableCollection<string> _DatabaseTables = new ObservableCollection<string>();
        public ObservableCollection<string> DatabaseTables
        {
            get
            {
                return _DatabaseTables;
            }
        }

        public DatabaseFile ActiveDatabase
        {
            get;
            set;
        }
    }


    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        public MainPage()
        {
            this.InitializeComponent();
        }

        public ViewModel ViewModel
        {
            get
            {
                return this.DataContext as ViewModel;
            }
        }

        private void OnMainPageLoaded(object sender, RoutedEventArgs e)
        {
            if ( !WinRTVFS.Initialize(true) )
            {
                throw new Exception("WinRTVFS failed to initialize");
            }
        }

        private async void OnSelectFolderButtonClick(object sender, RoutedEventArgs e)
        {
            FolderPicker folderPicker = new Windows.Storage.Pickers.FolderPicker();
            folderPicker.FileTypeFilter.Add(".db");
            folderPicker.FileTypeFilter.Add(".sqlite");
            StorageFolder selectedFolder = await folderPicker.PickSingleFolderAsync();

            Windows.Storage.AccessCache.StorageApplicationPermissions.FutureAccessList.Add(selectedFolder);

            string[] searchCriteria = { ".db", ".sqlite" };

            Windows.Storage.Search.QueryOptions queryOptions =
                new Windows.Storage.Search.QueryOptions(Windows.Storage.Search.CommonFileQuery.OrderByName, searchCriteria);
            queryOptions.FolderDepth = Windows.Storage.Search.FolderDepth.Shallow;

            var query = selectedFolder.CreateFileQueryWithOptions(queryOptions);
            var files = await query.GetFilesAsync();

            this.ViewModel.DatabaseFiles.Clear();
            foreach (StorageFile winFile in files)
            {
                this.ViewModel.DatabaseFiles.Add(
                    new DatabaseFile {  DisplayName = winFile.DisplayName, Path = winFile.Path }
                    );
            }
        }

        private async void OnFileSelected(object sender, ItemClickEventArgs e)
        {
            DatabaseFile dbf = e.ClickedItem as DatabaseFile;

            this.ViewModel.ActiveDatabase = dbf;
            this.ViewModel.DatabaseTables.Clear();

            try
            {
                StorageFile dbFile = await StorageFile.GetFileFromPathAsync(dbf.Path);
                SQLiteWinRT.Database db = new SQLiteWinRT.Database(
                    dbFile
                    );
                await db.OpenAsync(SQLiteWinRT.SqliteOpenMode.OpenRead);
                var stmt = await db.PrepareStatementAsync(
                    "SELECT name FROM sqlite_master WHERE type = 'table'"
                    );
                while ( await stmt.StepAsync() )
                {
                    string table = stmt.GetTextAt(0);
                    this.ViewModel.DatabaseTables.Add(table);
                }
            }
            catch 
            {

            }
        }
    }
}
