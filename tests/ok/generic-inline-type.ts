class ApiResponse<T> {
  success: boolean;
  data: T | null;
  constructor(partial: { success: boolean; data: T | null }) {
    this.success = partial.success;
    this.data = partial.data;
  }
  static ok<T>(data: T): ApiResponse<T> {
    return new ApiResponse<T>({ success: true, data });
  }
}
const a = ApiResponse.ok<number>(3);
const b = new ApiResponse<string>({ success: false, data: "x" });
console.log(a.success, a.data, b.success, b.data);
