import { Component, OnInit, SecurityContext } from "@angular/core";
import { DisplaysService } from "../displays.service";
import { Pipe, PipeTransform } from "@angular/core";
import { DomSanitizer, SafeResourceUrl } from "@angular/platform-browser";

@Component({
  selector: "app-view-pane",
  templateUrl: "./view-pane.component.html",
  styleUrls: ["./view-pane.component.sass"],
})
export class ViewPaneComponent implements OnInit {
  deviceURL: string = "";
  visibleDevices: string[] = [];

  constructor(
    public displaysService: DisplaysService,
    private sanitizer: DomSanitizer
  ) {}

  ngOnInit(): void {
    this.visibleDevices = this.displaysService.displays;
  }

  deviceConnectURL(display: string): string {
    return this.sanitizer.sanitize(
      SecurityContext.RESOURCE_URL,
      this.sanitizer.bypassSecurityTrustResourceUrl(
        `/devices/${display}/files/client.html`
      )
    ) as string;
  }
}
