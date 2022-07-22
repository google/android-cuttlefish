import { Injectable } from "@angular/core";

@Injectable({
  providedIn: "root",
})
export class DisplaysService {
  displays: string[] = [];

  add(display: string) {
    this.displays.push(display);
    return this.displays;
  }

  remove(display: string) {
    this.displays = this.displays.filter((element) => element !== display);
  }
}
