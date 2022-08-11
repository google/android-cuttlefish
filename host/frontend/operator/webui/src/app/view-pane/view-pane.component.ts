import {Component, OnInit, OnDestroy, SecurityContext} from '@angular/core';
import {DisplaysService} from '../displays.service';
import {DomSanitizer} from '@angular/platform-browser';
import {Subscription} from 'rxjs';

@Component({
  selector: 'app-view-pane',
  templateUrl: './view-pane.component.html',
  styleUrls: ['./view-pane.component.sass'],
})
export class ViewPaneComponent implements OnInit, OnDestroy {
  deviceURL = '';
  visibleDevices: string[] = [];
  private subscription!: Subscription;

  constructor(
    public displaysService: DisplaysService,
    private sanitizer: DomSanitizer
  ) {}

  ngOnInit(): void {
    this.subscription = this.displaysService
      .getVisibleDevices()
      .subscribe(display => {
        this.visibleDevices = display;
      });
  }

  ngOnDestroy(): void {
    this.subscription.unsubscribe();
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
