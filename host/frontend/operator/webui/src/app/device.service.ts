import { Injectable } from "@angular/core";
import { Observable, of, tap, map } from "rxjs";
import { HttpClient } from "@angular/common/http";

@Injectable({
  providedIn: "root",
})
export class DeviceService {

  constructor(private readonly httpClient: HttpClient) {}

  getDevices(): Observable<string[]> {
    return this.httpClient.get<string[]>("/devices");
  }
}
